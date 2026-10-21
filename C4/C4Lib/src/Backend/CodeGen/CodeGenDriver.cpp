#include "Backend/CodeGen/CodeGenDriver.h"
#include "Backend/CodeGen/DataImage.h"
#include "Parser/Nodes/Statements/AsmStatementNode.h"
#include "Backend/CodeGen/DebugInfo.h"
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include "Backend/CodeGen/GcRoots.h"
#include "Backend/CodeGen/TargetAttributes.h"
#include "Backend/CodeGen/VectorAlignment.h"
#include "Backend/CodeGen/VtableEmitter.h"
#include "Backend/CodeGen/TestHarnessEmitter.h"
#include "Backend/CodeGen/UseSites.h"
#include "Backend/Reflection/RegistrationEmitter.h"
#include "Script/ScriptModulePasses.h"
#include "Model/TypeHandle.h"
#include "Backend/HIR/HIR.h"
#include "Backend/HIR/GlobalInit.h"
#include "Backend/HIR/InvokeThunk.h"
#include "Model/Types/VirtualTables.h"
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

namespace Scripting
{
	CodeGenDriver::CodeGenDriver(llvm::LLVMContext& ctx) : m_Context(ctx), m_MCtx(ctx), m_Emitter(m_MCtx) {}

	namespace
	{
		bool IsWritten(const FunctionRecord& rec)
		{
			return rec.templateOrigin == TemplateOrigin::None && rec.synthesized == LifecycleOp::None;
		}

		// Where emission starts: every function written as it is that has a body of its own (a nested one comes with its
		// parent).
		Core::Vector<const FunctionRecord*> WrittenBodies(const Core::Vector<const FunctionRecord*>& records)
		{
			Core::Vector<const FunctionRecord*> roots;
			for (const FunctionRecord* rec : records)
				if (IsWritten(*rec) && HasBody(*rec) && !rec->isLocal) roots.Push(rec);
			return roots;
		}

		const FunctionRecord* EntryOf(const Core::Vector<const FunctionRecord*>& records, const Core::String& entryName)
		{
			for (const FunctionRecord* rec : records)
				if (IsEntryPoint(*rec, entryName)) return rec;
			return nullptr;
		}

		const char* MissingEntryMessage(const Core::Vector<const FunctionRecord*>& records)
		{
			for (const FunctionRecord* rec : records)
				if (!rec->containingType && !rec->isStatic && rec->mangledName.rfind("Main", 0) == 0 &&
					(rec->mangledName.size() == 4 || rec->mangledName[4] == '_'))
					return "Entry point must be named 'main' (lowercase); you defined 'Main'";
			return "No entry point: the program has no 'main' function";
		}
	}

	bool CodeGenDriver::EmitReachable(ProgramModel& model, Core::Vector<const FunctionRecord*> batch, std::string* errorMessage)
	{
		while (true)
		{
			for (const FunctionRecord* rec : m_MCtx.TakeNewFunctions()) batch.Push(rec);
			std::vector<const HIRFunction*> lowered;
			for (const FunctionRecord* rec : batch)
			{
				if (!HasBody(*rec) || rec->isLocal || m_Emitted.count(rec)) continue; // nested bodies come with their parent
				m_Emitted.insert(rec);
				const size_t errorsBefore = m_LoweringErrors.GetErrors().Count();
				m_Trees.push_back(std::make_unique<HIRFunction>(LowerFunction(model, m_LoweringErrors, rec)));
				lowered.push_back(m_Trees.back().get());
				NoteUse(model, *rec, errorsBefore);
			}
			batch.Clear();
			if (LoweringFailed(errorMessage)) return false;
			if (lowered.empty()) return true;
			for (const HIRFunction* tree : lowered)
			{
				if (!tree->complete) continue;
				m_Emitter.Emit(*tree);
				for (const FunctionRecord* rec : m_MCtx.TakeNewFunctions())
				{
					m_FirstUsers.emplace(rec, tree);
					batch.Push(rec);
				}
			}
		}
	}

	// A template-made body's diagnostics name the instance and where the program first uses it.
	void CodeGenDriver::NoteUse(const ProgramModel& model, const FunctionRecord& rec, size_t errorsBefore)
	{
		Core::Vector<CompilationError>& errors = m_LoweringErrors.GetErrorsMutable();
		if (errors.Count() == errorsBefore) return;
		const auto user = m_FirstUsers.find(&rec);
		const Core::String note = UseNote(model, rec, user == m_FirstUsers.end() ? nullptr : user->second);
		for (size_t i = errorsBefore; i < errors.Count(); ++i) errors[i].message += note;
	}

	bool CodeGenDriver::LoweringFailed(std::string* errorMessage) const
	{
		if (!m_LoweringErrors.HasErrors()) return false;
		std::string msg;
		for (const auto& e : m_LoweringErrors.GetErrors())
			if (e.severity != ErrorSeverity::Warning) msg += "lowering: " + std::string(e.message.c_str()) + "\n";
		if (errorMessage) *errorMessage = msg;
		return true;
	}

	bool CodeGenDriver::EmitGlobalInit(ProgramModel& model, std::string* errorMessage)
	{
		CheckConstantStarts(model, m_LoweringErrors);
		if (LoweringFailed(errorMessage)) return false;
		StartedGlobals started;
		// A script's rounds are its own: the host's carry these names too (docs/runtime/04 section 4.2).
		InitRounds process{ LowerGlobalInit, m_Script ? "__c4_script_init_round" : "__c4_global_init_round" };
		InitRounds thread{ LowerThreadInit, "__c4_thread_init_round" };
		for (bool more = true; more; )
		{
			if (m_Script) MarkHostGlobalsStarted(model, started);
			const size_t before = process.functions.size() + thread.functions.size() + m_FirstUseStarts.size();
			if (!EmitInitRound(model, process, started, errorMessage) || !EmitThreadStarts(model, thread, started, errorMessage)) return false;
			more = process.functions.size() + thread.functions.size() + m_FirstUseStarts.size() != before;
		}
		if (m_Script)
		{
			if (!process.functions.empty()) CallingInOrder("__c4_script_init", process.functions, nullptr);
			return EveryFirstUseStartDefined(errorMessage);
		}
		RegisterThreadStart(thread.functions);
		if (!process.functions.empty() || !thread.functions.empty()) RegisterGlobalInit(process.functions);
		return true;
	}

	bool CodeGenDriver::EmitInitRound(ProgramModel& model, InitRounds& rounds, StartedGlobals& started, std::string* errorMessage)
	{
		auto round = std::make_unique<HIRFunction>();
		std::vector<ConstantStart> constants;
		if (!rounds.lower(model, m_LoweringErrors, *round, started, constants)) return true;
		StartAsData(constants, *round);
		if (round->root->stmts.empty()) return true; // every global of the round starts as data
		if (!EmitReachable(model, {}, errorMessage)) return false; // reports the round's own failures too
		const std::string name = rounds.prefix + std::to_string(rounds.functions.size());
		rounds.functions.push_back(EmitStartFunction(model, std::move(round), name));
		return EmitReachable(model, {}, errorMessage);
	}

	void CodeGenDriver::StartAsData(std::vector<ConstantStart>& constants, HIRFunction& round)
	{
		for (ConstantStart& constant : constants)
		{
			llvm::GlobalVariable* storage = m_MCtx.GlobalAddress(*constant.variable);
			const llvm::DataLayout& layout = m_MCtx.module().getDataLayout();
			const bool fits = layout.getTypeAllocSize(storage->getValueType()) == constant.data.bytes.size();
			llvm::Constant* data = fits ? DataImage(storage->getValueType(), constant.data, 0, layout,
				[this](const ConstantValue& text) -> llvm::Constant* { return m_MCtx.StringGlobal(text); }) : nullptr;
			if (!data)
			{
				round.root->stmts.push_back(constant.start);
				continue;
			}
			storage->setInitializer(data);
			if (constant.variable->isConst) storage->setConstant(true);
		}
	}

	bool CodeGenDriver::EmitThreadStarts(ProgramModel& model, InitRounds& rounds, StartedGlobals& started, std::string* errorMessage)
	{
		return m_Script ? EmitFirstUseStarts(model, started, errorMessage) : EmitInitRound(model, rounds, started, errorMessage);
	}

	bool CodeGenDriver::EmitFirstUseStarts(ProgramModel& model, StartedGlobals& started, std::string* errorMessage)
	{
		std::vector<FirstUseStart> starts = LowerFirstUseStarts(model, m_LoweringErrors, started);
		if (starts.empty()) return true;
		if (!EmitReachable(model, {}, errorMessage)) return false; // reports the starts' own failures too
		for (FirstUseStart& start : starts)
		{
			// The declaration code that uses the variable already calls (ModuleContext::FirstUseStart).
			llvm::Function* declared = m_MCtx.FirstUseStart(*start.variable);
			llvm::Function* function = EmitStartFunction(model, std::move(start.function), declared->getName().str());
			function->setLinkage(llvm::GlobalValue::InternalLinkage);
			m_FirstUseStarts.push_back(function);
		}
		return EmitReachable(model, {}, errorMessage);
	}

	bool CodeGenDriver::EveryFirstUseStartDefined(std::string* errorMessage) const
	{
		for (const auto& [variable, start] : m_MCtx.FirstUseStarts())
		{
			if (!start->isDeclaration()) continue;
			if (errorMessage) *errorMessage = "internal: no start was lowered for the thread_local '" + std::string(variable->mangledName.c_str()) + "'";
			return false;
		}
		return true;
	}

	llvm::Function* CodeGenDriver::EmitStartFunction(ProgramModel& model, std::unique_ptr<HIRFunction> body, const std::string& name)
	{
		FunctionRecord& record = m_GlobalInitRounds.emplace_back();
		record.type = model.TypeStorePtr()->Function(model.TypeStorePtr()->Builtin(TypeKind::Void), {});
		record.mangledName = name.c_str();
		body->record = &record;
		body->complete = true;
		m_Emitter.Emit(*body);
		m_Trees.push_back(std::move(body));
		return m_MCtx.GetOrCreateFunction(record);
	}

	bool CodeGenDriver::EmitInvokeThunks(ProgramModel& model, std::string* errorMessage)
	{
		while (true)
		{
			Core::Vector<const FunctionRecord*> targets = NewInvokeTargets(model);
			if (targets.Empty()) return true;
			for (const FunctionRecord* target : targets) EmitInvokeThunk(model, *target);
			if (!EmitReachable(model, {}, errorMessage)) return false;
		}
	}

	void CodeGenDriver::EmitInvokeThunk(ProgramModel& model, const FunctionRecord& target)
	{
		if (!SharesInvokeThunk(model, target))
		{
			if (llvm::Function* own = LowerInvokeThunk(model, target, InvokeThunkSymbol(target), false)) m_Thunks[&target] = { own, nullptr };
			return;
		}
		const std::string symbol = SharedInvokeThunkSymbol(target);
		auto [entry, fresh] = m_SharedThunks.try_emplace(symbol, nullptr);
		if (fresh) entry->second = LowerInvokeThunk(model, target, symbol, true);
		if (entry->second) m_Thunks[&target] = { entry->second, m_MCtx.GetOrCreateFunction(target) };
	}

	llvm::Function* CodeGenDriver::LowerInvokeThunk(ProgramModel& model, const FunctionRecord& target, const std::string& symbol, bool shared)
	{
		FunctionRecord& thunk = m_ThunkRecords.emplace_back();
		thunk.type = InvokeThunkType(model);
		thunk.mangledName = symbol.c_str();
		ErrorTracker notCallable; // reflection reports the function as not invokable instead
		auto tree = std::make_unique<HIRFunction>(Scripting::LowerInvokeThunk(model, notCallable, target, thunk, shared));
		if (!tree->complete) return nullptr;
		m_Emitter.Emit(*tree);
		m_Trees.push_back(std::move(tree));
		llvm::Function* emitted = m_MCtx.GetOrCreateFunction(thunk);
		emitted->setLinkage(llvm::GlobalValue::InternalLinkage); // only the reflection image names it
		// Reflection calls through it seldom: small, with its target called rather than copied into it.
		emitted->addFnAttr(llvm::Attribute::MinSize);
		emitted->addFnAttr(llvm::Attribute::OptimizeForSize);
		return emitted;
	}

	Core::Vector<const FunctionRecord*> CodeGenDriver::NewInvokeTargets(ProgramModel& model)
	{
		Core::Vector<const FunctionRecord*> records, targets;
		model.AllFunctionRecords(records);
		for (const FunctionRecord* rec : records)
			if (ReflectedFunction(*rec) && Invokable(*rec) && !IsHostSymbol(rec->mangledName.c_str()) && HasCallableCode(model, *rec) &&
				m_ThunkTargets.insert(rec).second)
				targets.Push(rec);
		return targets;
	}

	bool CodeGenDriver::HasCallableCode(const ProgramModel& model, const FunctionRecord& rec)
	{
		if (m_Emitted.count(&rec) && !m_MCtx.GetOrCreateFunction(rec)->isDeclaration()) return true;
		const ObjectType::MethodInfo* method = model.MethodForRecord(rec);
		return method && VirtualTables::IsVirtual(rec.containingType, *method);
	}

	llvm::Function* CodeGenDriver::CallingInOrder(const char* name, const std::vector<llvm::Function*>& calls, llvm::Function* last)
	{
		auto* voidFn = llvm::FunctionType::get(llvm::Type::getVoidTy(m_Context), false);
		auto* fn = llvm::Function::Create(voidFn, llvm::Function::ExternalLinkage, name, m_MCtx.module());
		llvm::IRBuilder<> B(llvm::BasicBlock::Create(m_Context, "entry", fn));
		for (llvm::Function* call : calls) B.CreateCall(call, {});
		if (last) B.CreateCall(last, {});
		B.CreateRetVoid();
		return fn;
	}

	void CodeGenDriver::RegisterThreadStart(const std::vector<llvm::Function*>& rounds)
	{
		// The runtime runs it on every thread that attaches (C4Runtime/ThreadRuntimeC.c).
		CallingInOrder("__c4_thread_start", rounds, nullptr);
	}

	void CodeGenDriver::RegisterGlobalInit(const std::vector<llvm::Function*>& rounds)
	{
		// The main thread attaches last: its thread_locals start once every global has.
		llvm::Function* attach = m_MCtx.RuntimeFunction("c4_thread_attach", llvm::Type::getVoidTy(m_Context), {});
		llvm::Function* init = CallingInOrder("__c4_global_init", rounds, attach);

		// The C runtime runs @llvm.global_ctors before main; priority 65535 runs after every C++ static constructor.
		llvm::appendToGlobalCtors(m_MCtx.module(), init, kLastConstructorPriority);
	}

	void CodeGenDriver::InstallGcStatsPrinter()
	{
		auto* voidFn = llvm::FunctionType::get(llvm::Type::getVoidTy(m_Context), false);
		llvm::FunctionCallee install = m_MCtx.module().getOrInsertFunction("c4_gc_install_default_stats_printer", voidFn);
		auto* starter = llvm::Function::Create(voidFn, llvm::Function::InternalLinkage, "__c4_install_gc_stats", m_MCtx.module());
		llvm::IRBuilder<> B(llvm::BasicBlock::Create(m_Context, "entry", starter));
		B.CreateCall(install);
		B.CreateRetVoid();
		llvm::appendToGlobalCtors(m_MCtx.module(), starter, kLastConstructorPriority);
	}

	bool CodeGenDriver::EmitEveryVtable(ProgramModel& model, std::string* errorMessage)
	{
		for (const ObjectType* type : model.ObjectTypes()) m_MCtx.vtables().VptrSites(type);
		return EmitReachable(model, {}, errorMessage);
	}

	void CodeGenDriver::MaterializeGlobals(ProgramModel& model)
	{
		model.Symbols()->ForEachGlobalVariable([&](const SymbolInfo& info, const Scope& declaredIn)
			{
				if (info.kind != SymbolKind::Variable || !info.type) return;
				llvm::GlobalVariable* gv = m_MCtx.GlobalAddress(info);
				if (DebugInfo* debug = m_MCtx.Debug()) debug->DescribeGlobal(*gv, info, declaredIn);
			});
	}

	bool CodeGenDriver::EmitReflection(ProgramModel& model, std::string* errorMessage)
	{
		const HostEntities host{ m_Script ? m_Script->hostSymbols : nullptr, m_Script ? m_Script->hostTypes : nullptr,
			m_Script ? m_Script->interfaceUnits : 0 };
		llvm::GlobalVariable* image = EmitReflectionImage(m_MCtx, model, m_Thunks, host, m_LoweringErrors);
		if (LoweringFailed(errorMessage)) return false;
		if (!image || m_Script) return true; // a script's loader registers the image under the script's tag
		auto* ptrTy = llvm::PointerType::get(m_Context, 0);
		llvm::Function* registerImage = m_MCtx.RuntimeFunction("c4_reflect_register_image", llvm::Type::getVoidTy(m_Context), { ptrTy, ptrTy });
		auto* voidFn = llvm::FunctionType::get(llvm::Type::getVoidTy(m_Context), false);
		auto* init = llvm::Function::Create(voidFn, llvm::Function::InternalLinkage, "__c4_reflect_init", m_MCtx.module());
		llvm::IRBuilder<> B(llvm::BasicBlock::Create(m_Context, "entry", init));
		B.CreateCall(registerImage, { m_MCtx.StringGlobal(ConstantValue::String(m_ReflectionTag.c_str())), image });
		B.CreateRetVoid();
		llvm::appendToGlobalCtors(m_MCtx.module(), init, kReflectionPriority);
		return true;
	}

	bool CodeGenDriver::IsHostSymbol(const std::string& symbol) const
	{
		return m_Script && m_Script->hostSymbols && m_Script->hostSymbols->count(symbol);
	}

	void CodeGenDriver::MarkHostGlobalsStarted(ProgramModel& model, StartedGlobals& started) const
	{
		model.Symbols()->CollectGlobalVariables([&](const SymbolInfo& sym) {
			if (IsHostStorage(sym)) started.insert(&sym);
		});
	}

	bool CodeGenDriver::IsHostStorage(const SymbolInfo& sym) const
	{
		const std::string symbol = sym.mangledName.c_str();
		const bool declaredByHost = sym.definingUnit >= 1 && sym.definingUnit <= m_Script->interfaceUnits;
		return declaredByHost || IsHostSymbol(symbol) || IsHostSymbol(ThreadLocalAccessor(symbol));
	}

	void CodeGenDriver::ApplyScriptLowering()
	{
		UseHostThreadStorage(m_MCtx.module(), *m_Script->hostSymbols);
		UseHostDefinitions(m_MCtx.module(), *m_Script->hostSymbols);
		UseScriptRuntime(m_MCtx.module());
	}

	bool CodeGenDriver::Lower(ProgramModel& model, const Core::String& entryName, std::string* errorMessage, const TestHarnessOptions* tests)
	{
		setvbuf(stderr, nullptr, _IONBF, 0); // unbuffered so EH debug markers survive a crash
		LowerTypes(model);
		EmitFileAssembly(model);
		if (m_Script)
			m_MCtx.StartThreadLocalsOnFirstUse([this, &model](const SymbolInfo& sym) { return !IsHostStorage(sym) && StartsWithCode(model, sym); });
		Core::Vector<const FunctionRecord*> records;
		model.AllFunctionRecords(records);
		DeclareWrittenFunctions(records);
		if (!EmitReachable(model, WrittenBodies(records), errorMessage)) return false;
		const bool harness = tests && TestHarnessEmitter(m_MCtx, model.Tests(), *tests).Emit();
		if (!harness && !entryName.empty() && !EmitEntryPoint(records, entryName, errorMessage)) return false;
		if (!EmitReachable(model, {}, errorMessage)) return false; // what the test harness or the entry wrapper references
		if (!EmitStaticState(model, errorMessage)) return false;
		if (!EmitReflection(model, errorMessage)) return false;
		FinishModule(model);
		return Verify(errorMessage);
	}

	void CodeGenDriver::EmitFileAssembly(const ProgramModel& model)
	{
		// Intel syntax unless the block says att_syntax; each block switches back so the next one starts in AT&T,
		// the module assembler's own default.
		for (const AsmStatementNode* block : model.FileAssembly())
		{
			const bool att = !block->GetOptions().Empty();
			std::string text = att ? "" : ".intel_syntax noprefix\n";
			for (const Core::String& line : block->GetLines()) text += std::string(line.c_str()) + "\n";
			if (!att) text += ".att_syntax prefix\n";
			m_MCtx.module().appendModuleInlineAsm(text);
		}
	}

	void CodeGenDriver::LowerTypes(ProgramModel& model)
	{
		m_MCtx.SetModel(&model); // GlobalAddress resolves funcptr initializers (`funcptr<R(a)> g = F;`) against it
		if (m_DebugInfo) m_MCtx.EnableDebugInfo(model.SourcePaths());
		for (const ObjectType* obj : model.ObjectTypes())
			m_MCtx.FillStruct(obj);
	}

	// A runtime or interop name is declared once, by its record; template-made and compiler-written functions are
	// created when first referenced.
	void CodeGenDriver::DeclareWrittenFunctions(const Core::Vector<const FunctionRecord*>& records)
	{
		for (const FunctionRecord* rec : records)
			if (IsWritten(*rec)) m_MCtx.GetOrCreateFunction(*rec);
	}

	// The linker looks for a literal @main; the program's entry has an overload suffix (`main_0`), so a thin @main
	// forwards to it and returns its value.
	bool CodeGenDriver::EmitEntryPoint(const Core::Vector<const FunctionRecord*>& records, const Core::String& entryName, std::string* errorMessage)
	{
		const FunctionRecord* entry = EntryOf(records, entryName);
		if (!entry)
		{
			if (errorMessage) *errorMessage = MissingEntryMessage(records);
			return false;
		}
		if (entry->mangledName != "main") EmitMainWrapper(*entry);
		return true;
	}

	// The wrapper mirrors the entry's parameters, so `main(int32 argc, char8** argv)` gets the real command line; the
	// types come from the callee's own LLVM signature, so the call matches exactly. A void entry exits with 0.
	void CodeGenDriver::EmitMainWrapper(const FunctionRecord& entry)
	{
		auto* callee = m_MCtx.GetOrCreateFunction(entry);
		const auto* calleeType = llvm::cast<llvm::FunctionType>(callee->getFunctionType());
		auto* exitCodeType = llvm::IntegerType::get(m_Context, 32);
		auto* mainType = llvm::FunctionType::get(exitCodeType, calleeType->params(), calleeType->isVarArg());
		auto* mainFn = llvm::Function::Create(mainType, llvm::Function::ExternalLinkage, "main", &m_MCtx.module());
		llvm::IRBuilder<> B(llvm::BasicBlock::Create(m_Context, "entry", mainFn)); // Create leaves the function empty
		std::vector<llvm::Value*> arguments;
		for (llvm::Argument& argument : mainFn->args())
			arguments.push_back(&argument);
		llvm::Value* result = B.CreateCall(callee, arguments);
		if (result && !result->getType()->isVoidTy()) B.CreateRet(result);
		else B.CreateRet(llvm::ConstantInt::get(exitCodeType, 0));
	}

	// Invoke thunks and global initialization come after every body, so the template instances the bodies and thunks
	// make start their static data members too; the functions global initialization reaches get their thunks after it.
	bool CodeGenDriver::EmitStaticState(ProgramModel& model, std::string* errorMessage)
	{
		if (m_ShareVtables && !EmitEveryVtable(model, errorMessage)) return false;
		if (!EmitInvokeThunks(model, errorMessage)) return false;
		if (!EmitGlobalInit(model, errorMessage)) return false;
		if (!EmitInvokeThunks(model, errorMessage)) return false;
		MaterializeGlobals(model);
		return true;
	}

	// Every body is emitted, so every static local and every global exists for the GC roots.
	void CodeGenDriver::FinishModule(ProgramModel& model)
	{
		EmitGcRoots(m_MCtx, model, m_Script ? m_Script->hostSymbols : nullptr);
		if (m_Script) ApplyScriptLowering(); // after the roots: it removes host thread_locals the module's caches point at
		AlignVectorAccesses(m_MCtx.module());
		ApplyTarget(m_MCtx.module(), model.Target());
		if (DebugInfo* debug = m_MCtx.Debug()) debug->Finish();
	}

	// The module is verified once (which checks every function too); only a failure goes through the functions one by
	// one, so the diagnostic names the culprit (a bare verifyModule reports the first error without saying where).
	bool CodeGenDriver::Verify(std::string* errorMessage)
	{
		if (!llvm::verifyModule(m_MCtx.module())) return true;
		std::string err;
		llvm::raw_string_ostream os(err);
		for (llvm::Function& F : m_MCtx.module())
			if (llvm::verifyFunction(F, &os))
			{
				if (errorMessage) *errorMessage = "IR verification failed in function '" + F.getName().str() + "': " + err;
				return false;
			}
		llvm::verifyModule(m_MCtx.module(), &os);
		if (errorMessage) *errorMessage = "IR verification failed: " + err;
		return false;
	}
} // namespace Scripting
