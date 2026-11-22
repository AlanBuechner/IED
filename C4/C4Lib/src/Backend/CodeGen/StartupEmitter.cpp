#include "Backend/CodeGen/StartupEmitter.h"
#include "Backend/CodeGen/DataImage.h"
#include "Backend/MIR/MirBody.h"
#include "Model/ProgramModel.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

namespace C4Compiler
{
	namespace
	{
		/// The constructor priority that runs after every C and C++ static constructor.
		constexpr int kLastConstructorPriority = 65535;

		const FunctionRecord* EntryOf(const Core::Vector<const FunctionRecord*>& records, const Core::String& entryName)
		{
			for (const FunctionRecord* record : records)
				if (IsEntryPoint(*record, entryName)) return record;
			return nullptr;
		}

		const char* MissingEntryMessage(const Core::Vector<const FunctionRecord*>& records)
		{
			for (const FunctionRecord* record : records)
				if (!record->containingType && !record->isStatic && record->mangledName.rfind("Main", 0) == 0 &&
					(record->mangledName.size() == 4 || record->mangledName[4] == '_'))
					return "Entry point must be named 'main' (lowercase); you defined 'Main'";
			return "No entry point: the program has no 'main' function";
		}
	}

	bool StartupEmitter::EmitStarts(std::vector<LoweredUnit>& units, ProgramModel& model, std::string* error)
	{
		// A script's rounds are its own: the host's carry these names too (docs/runtime/04 section 4.2).
		Rounds process{ m_Script ? "__c4_script_init_round" : "__c4_global_init_round" };
		Rounds thread{ "__c4_thread_init_round" };
		for (LoweredUnit& unit : units)
		{
			if (unit.kind == UnitKind::ProcessStarts) EmitRound(model, unit, process);
			else if (unit.kind == UnitKind::ThreadStarts) EmitRound(model, unit, thread);
			else if (unit.kind == UnitKind::FirstUseStart) EmitFirstUseStart(model, unit);
		}
		if (m_Script)
		{
			if (!process.functions.empty()) CallingInOrder("__c4_script_init", process.functions, nullptr);
			return EveryFirstUseStartDefined(error);
		}
		RegisterThreadStart(thread.functions);
		if (!process.functions.empty() || !thread.functions.empty()) RegisterGlobalInit(process.functions);
		return true;
	}

	void StartupEmitter::EmitRound(ProgramModel& model, LoweredUnit& round, Rounds& rounds)
	{
		StartAsData(round.constants, *round.tree);
		if (round.tree->root->stmts.empty()) return; // every global of the round starts as data
		const std::string name = rounds.prefix + std::to_string(rounds.functions.size());
		rounds.functions.push_back(EmitStartFunction(model, *round.tree, name));
	}

	// The declaration code that uses the variable already calls (ModuleContext::FirstUseStart).
	void StartupEmitter::EmitFirstUseStart(ProgramModel& model, LoweredUnit& start)
	{
		llvm::Function* declared = m_Partitions.Partition(0).FirstUseStart(*start.variable);
		llvm::Function* function = EmitStartFunction(model, *start.tree, declared->getName().str());
		function->setLinkage(llvm::GlobalValue::InternalLinkage);
		m_FirstUseStarts.push_back(function);
	}

	void StartupEmitter::StartAsData(std::vector<ConstantStart>& constants, HIRFunction& round)
	{
		bool started = false;
		for (ConstantStart& constant : constants)
		{
			ModuleContext& home = m_Partitions.Partition(m_Partitions.Of(*constant.variable));
			llvm::GlobalVariable* storage = home.GlobalAddress(*constant.variable);
			const llvm::DataLayout& layout = home.module().getDataLayout();
			const bool fits = layout.getTypeAllocSize(storage->getValueType()) == constant.data.bytes.size();
			llvm::Constant* data = fits ? DataImage(storage->getValueType(), constant.data, 0, layout,
				[&home](const ConstantValue& text) -> llvm::Constant* { return home.StringGlobal(text); }) : nullptr;
			if (!data)
			{
				round.root->stmts.push_back(constant.start);
				started = true;
				continue;
			}
			storage->setInitializer(data);
			if (constant.variable->isConst) storage->setConstant(true);
		}
		// Only the storage's type tells whether a start fits as data: one that does not runs in the round, so the round's
		// MIR is built again with it.
		if (!started) return;
		round.mir.reset();
		EnsureMir(round);
	}

	llvm::Function* StartupEmitter::EmitStartFunction(ProgramModel& model, HIRFunction& body, const std::string& name)
	{
		FunctionRecord& record = m_Records.emplace_back();
		record.type = model.TypeStorePtr()->Function(model.TypeStorePtr()->Builtin(TypeKind::Void), {});
		record.mangledName = name.c_str();
		body.record = &record;
		m_Partitions.EmitterOf(0).Emit(body);
		return m_Partitions.Partition(0).GetOrCreateFunction(record);
	}

	bool StartupEmitter::EveryFirstUseStartDefined(std::string* error)
	{
		for (const auto& [variable, start] : m_Partitions.Partition(0).FirstUseStarts())
		{
			if (!start->isDeclaration()) continue;
			if (error) *error = "internal: no start was lowered for the thread_local '" + std::string(variable->mangledName.c_str()) + "'";
			return false;
		}
		return true;
	}

	void StartupEmitter::RegisterThreadStart(const std::vector<llvm::Function*>& rounds)
	{
		// The runtime runs it on every thread that attaches (C4Runtime/ThreadRuntimeC.c).
		CallingInOrder("__c4_thread_start", rounds, nullptr);
	}

	void StartupEmitter::RegisterGlobalInit(const std::vector<llvm::Function*>& rounds)
	{
		// The main thread attaches last: its thread_locals start once every global has.
		ModuleContext& program = m_Partitions.Partition(0);
		llvm::Function* attach = program.RuntimeFunction("c4_thread_attach", llvm::Type::getVoidTy(program.context()), {});
		llvm::Function* init = CallingInOrder("__c4_global_init", rounds, attach);
		// The C runtime runs @llvm.global_ctors before main; this priority runs after every C++ static constructor.
		llvm::appendToGlobalCtors(program.module(), init, kLastConstructorPriority);
	}

	llvm::Function* StartupEmitter::CallingInOrder(const char* name, const std::vector<llvm::Function*>& calls, llvm::Function* last)
	{
		ModuleContext& program = m_Partitions.Partition(0);
		auto* voidFn = llvm::FunctionType::get(llvm::Type::getVoidTy(program.context()), false);
		auto* fn = llvm::Function::Create(voidFn, llvm::Function::ExternalLinkage, name, program.module());
		llvm::IRBuilder<> B(llvm::BasicBlock::Create(program.context(), "entry", fn));
		for (llvm::Function* call : calls) B.CreateCall(call, {});
		if (last) B.CreateCall(last, {});
		B.CreateRetVoid();
		return fn;
	}

	bool StartupEmitter::EmitEntryPoint(const ProgramModel& model, const Core::String& entryName, std::string* error)
	{
		Core::Vector<const FunctionRecord*> records;
		model.AllFunctionRecords(records);
		const FunctionRecord* entry = EntryOf(records, entryName);
		if (!entry)
		{
			if (error) *error = MissingEntryMessage(records);
			return false;
		}
		if (entry->mangledName != "main") EmitMainWrapper(*entry);
		return true;
	}

	// The wrapper mirrors the entry's parameters, so `main(int32 argc, char8** argv)` gets the real command line; the
	// types come from the callee's own LLVM signature, so the call matches exactly. A void entry exits with 0.
	void StartupEmitter::EmitMainWrapper(const FunctionRecord& entry)
	{
		ModuleContext& program = m_Partitions.Partition(0);
		auto* callee = program.GetOrCreateFunction(entry);
		const auto* calleeType = llvm::cast<llvm::FunctionType>(callee->getFunctionType());
		auto* exitCodeType = llvm::IntegerType::get(program.context(), 32);
		auto* mainType = llvm::FunctionType::get(exitCodeType, calleeType->params(), calleeType->isVarArg());
		auto* mainFn = llvm::Function::Create(mainType, llvm::Function::ExternalLinkage, "main", &program.module());
		llvm::IRBuilder<> B(llvm::BasicBlock::Create(program.context(), "entry", mainFn)); // Create leaves the function empty
		std::vector<llvm::Value*> arguments;
		for (llvm::Argument& argument : mainFn->args()) arguments.push_back(&argument);
		llvm::CallInst* result = B.CreateCall(callee, arguments);
		result->setIsNoInline(); // the wrapper has no debug info: the program's main inlined into it would have no lines
		if (!result->getType()->isVoidTy()) B.CreateRet(result);
		else B.CreateRet(llvm::ConstantInt::get(exitCodeType, 0));
	}

	void StartupEmitter::InstallGcStatsPrinter()
	{
		ModuleContext& program = m_Partitions.Partition(0);
		auto* voidFn = llvm::FunctionType::get(llvm::Type::getVoidTy(program.context()), false);
		llvm::FunctionCallee install = program.module().getOrInsertFunction("c4_gc_install_default_stats_printer", voidFn);
		auto* starter = llvm::Function::Create(voidFn, llvm::Function::InternalLinkage, "__c4_install_gc_stats", program.module());
		llvm::IRBuilder<> B(llvm::BasicBlock::Create(program.context(), "entry", starter));
		B.CreateCall(install);
		B.CreateRetVoid();
		llvm::appendToGlobalCtors(program.module(), starter, kLastConstructorPriority);
	}
} // namespace C4Compiler
