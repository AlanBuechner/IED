#include "StackMapsTest.h"
#include "Backend/ProgramBackend.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/ObjectEmission.h"
#include "Compiler/RuntimeObjects.h"
#include "Compiler/StackMaps.h"
#include "Compiler/StackMapValues.h"
#include "Compiler/Toolchain.h"
#include <llvm/AsmParser/Parser.h>
#include <llvm/BinaryFormat/ELF.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Object/ELFObjectFile.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Testing
{
	using namespace C4Compiler;

	namespace
	{
		// `a`, `b` and `c` are read after the third `new`, only `c` after Call: at -O0 each is a stack slot.
		constexpr const char* kProgram = R"(
			struct T { public int32 v; }
			int32 g_calls = 0;
			void Call() { g_calls++; }
			void Use(T* t) { t->v++; }
			int32 Slots() {
				T* a = new T;
				T* b = new T;
				T* c = new T;
				a->v = b->v + c->v;
				Call();
				return c->v;
			}
			int32 main() {
				T* t = new T;
				Use(t);
				return Slots();
			}
		)";

		struct Lowered
		{
			std::unique_ptr<llvm::LLVMContext> context = std::make_unique<llvm::LLVMContext>();
			std::unique_ptr<llvm::Module> module;
			std::string ir;
			std::string errors;
		};

		std::string Text(const llvm::Module& module)
		{
			std::string text;
			llvm::raw_string_ostream out(text);
			module.print(out, nullptr);
			return text;
		}

		// A type as IR writes it: two modules' types are compared by it (each module has a context of its own).
		std::string Text(const llvm::Type* type)
		{
			std::string text;
			llvm::raw_string_ostream out(text);
			type->print(out);
			return text;
		}

		Lowered Lower(const char* source, bool stackMaps, TargetSystem system = HostSystem())
		{
			CompilerInstance compiler;
			compiler.RegisterInMemory("t.cf", source);
			compiler.RegisterIncludedLibraryFiles(Toolchain::Bedrock());
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			Lowered out;
			ProgramBackend driver(*out.context, system);
			if (stackMaps) driver.UseStackMaps();
			std::string error;
			const bool ok = driver.Lower(compiler.GetProgramModelForLowering(), "main", &error);
			for (const CompilationError& e : driver.LoweringErrors()) out.errors += std::string(e.message.c_str()) + "\n";
			out.errors += error;
			if (!ok) return out;
			out.module = driver.TakeModule();
			out.ir = Text(*out.module);
			return out;
		}

		// A module written as IR, for a target, marked for stack maps.
		std::unique_ptr<llvm::Module> Parsed(llvm::LLVMContext& context, const std::string& body, TargetSystem system = TargetSystem::Windows)
		{
			const char* triple = system == TargetSystem::Linux ? "x86_64-unknown-linux-gnu" : "x86_64-pc-windows-msvc";
			llvm::SMDiagnostic error;
			std::unique_ptr<llvm::Module> module = llvm::parseAssemblyString(std::string("target triple = \"") + triple + "\"\n" + body, error, context);
			if (!module)
			{
				std::string message;
				llvm::raw_string_ostream out(message);
				error.print("stack maps test", out);
				printf("%s\n", message.c_str());
				return nullptr;
			}
			MarkForStackMaps(*module);
			return module;
		}

		const llvm::Function* FunctionNamed(const llvm::Module& module, const char* prefix)
		{
			for (const llvm::Function& fn : module)
				if (!fn.isDeclaration() && fn.getName().starts_with(prefix)) return &fn;
			return nullptr;
		}

		bool Verified(const llvm::Module& module)
		{
			std::string problems;
			llvm::raw_string_ostream out(problems);
			const bool broken = llvm::verifyModule(module, &out);
			if (broken) printf("%s\n", problems.c_str());
			return !broken;
		}

		// The calls of `fn` with a record (a deopt list) whose callee's name starts with `prefix`.
		std::vector<const llvm::CallBase*> RecordedCalls(const llvm::Function& fn, const char* prefix)
		{
			std::vector<const llvm::CallBase*> found;
			for (const llvm::Instruction& instruction : llvm::instructions(fn))
				if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction))
					if (const llvm::Function* callee = call->getCalledFunction(); callee && callee->getName().starts_with(prefix) && call->getOperandBundle(llvm::LLVMContext::OB_deopt))
						found.push_back(call);
			return found;
		}

		// How many stack slots a call lists (Compiler/StackMapSlots: each slot, then its words and masks).
		size_t ListedSlots(const llvm::CallBase& call)
		{
			const std::optional<llvm::OperandBundleUse> deopt = call.getOperandBundle(llvm::LLVMContext::OB_deopt);
			size_t slots = 0;
			if (deopt)
				for (const llvm::Use& input : deopt->Inputs) slots += llvm::isa<llvm::AllocaInst>(input.get()) ? 1 : 0;
			return slots;
		}

		// The names of the values ValuesLiveAcross gives for the call of `callee` in `fn`.
		std::vector<std::string> LiveAcross(llvm::Function& fn, const char* callee)
		{
			std::vector<llvm::CallBase*> calls;
			for (llvm::Instruction& instruction : llvm::instructions(fn))
				if (auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction); call && call->getCalledFunction() && call->getCalledFunction()->getName() == callee)
					calls.push_back(call);
			std::vector<std::string> names;
			if (calls.size() != 1) return names;
			const auto live = ValuesLiveAcross(fn, calls);
			const auto found = live.find(calls[0]);
			if (found != live.end())
				for (const llvm::Value* value : found->second) names.push_back(value->getName().str());
			return names;
		}

		// Whether code generation writes `module` as an object for `system`.
		bool ObjectWritten(llvm::Module& module, TargetSystem system, int optLevel)
		{
			const CpuTarget target = CpuTarget().On(system);
			const ObjectSettings settings{ optLevel, &target, 1, false };
			const std::string path = (std::filesystem::temp_directory_path() / "c4_stack_maps_test_object.o").string();
			std::vector<ObjectDiagnostic> diagnostics;
			const bool written = WriteObject(module, settings, path, diagnostics);
			for (const ObjectDiagnostic& diagnostic : diagnostics) printf("%s\n", diagnostic.message.c_str());
			std::error_code ec;
			std::filesystem::remove(path, ec);
			return written && diagnostics.empty();
		}
	}

	void StackMapsTest::WithoutTheFlagNothingChanges()
	{
		Lowered plain = Lower(kProgram, false);
		TEST_ASSERT(plain.module != nullptr);
		if (!plain.module) return;
		TEST_ASSERT(!UsesStackMaps(*plain.module));
		InsertStatepoints(*plain.module);
		TEST_ASSERT(Text(*plain.module) == plain.ir);
	}

	void StackMapsTest::TheFlagChangesNoFunction()
	{
		Lowered plain = Lower(kProgram, false);
		Lowered mapped = Lower(kProgram, true);
		TEST_ASSERT(plain.module != nullptr && mapped.module != nullptr);
		if (!plain.module || !mapped.module) return;
		TEST_ASSERT(UsesStackMaps(*mapped.module));
		TEST_ASSERT(mapped.module->getDataLayoutStr() == plain.module->getDataLayoutStr());
		TEST_ASSERT(mapped.ir.find("addrspace(") == std::string::npos);
		for (const llvm::Function& fn : *plain.module)
		{
			const llvm::Function* same = mapped.module->getFunction(fn.getName());
			const bool alike = same != nullptr && Text(same->getFunctionType()) == Text(fn.getFunctionType()) && same->getInstructionCount() == fn.getInstructionCount();
			if (!alike) printf("not alike: %s (%u instructions, %u with the flag)\n", fn.getName().str().c_str(), fn.getInstructionCount(), same ? same->getInstructionCount() : 0u);
			TEST_ASSERT(alike);
			if (!alike) continue;
			auto other = llvm::instructions(*same).begin();
			for (const llvm::Instruction& instruction : llvm::instructions(fn))
			{
				TEST_ASSERT(instruction.getOpcode() == other->getOpcode() && Text(instruction.getType()) == Text(other->getType()));
				++other;
			}
		}
	}

	void StackMapsTest::CallsGetRecords()
	{
		Lowered mapped = Lower(kProgram, true);
		TEST_ASSERT(mapped.module != nullptr);
		if (!mapped.module) return;
		InsertStatepoints(*mapped.module);
		TEST_ASSERT(Verified(*mapped.module));
		const llvm::Function* slots = FunctionNamed(*mapped.module, "Slots");
		TEST_ASSERT(slots && slots->hasGC() && slots->getGC() == c_StackMapsGc);
		if (slots) TEST_ASSERT(RecordedCalls(*slots, "Call").size() == 1);
		TEST_ASSERT(ObjectWritten(*mapped.module, HostSystem(), 0));
	}

	void StackMapsTest::ADeadSlotIsNotListed()
	{
		Lowered mapped = Lower(kProgram, true);
		TEST_ASSERT(mapped.module != nullptr);
		if (!mapped.module) return;
		InsertStatepoints(*mapped.module);
		const llvm::Function* slots = FunctionNamed(*mapped.module, "Slots");
		TEST_ASSERT(slots != nullptr);
		if (!slots) return;
		const std::vector<const llvm::CallBase*> calls = RecordedCalls(*slots, "Call");
		const std::vector<const llvm::CallBase*> allocations = RecordedCalls(*slots, "c4_alloc");
		TEST_ASSERT(calls.size() == 1 && allocations.size() == 3);
		if (calls.size() != 1 || allocations.size() != 3) return;
		// At the third `new` a and b are read later; at Call only c is.
		TEST_ASSERT(ListedSlots(*allocations[2]) >= 2);
		TEST_ASSERT(ListedSlots(*calls[0]) >= 1);
		TEST_ASSERT(ListedSlots(*calls[0]) < ListedSlots(*allocations[2]));
	}

	void StackMapsTest::AValueLiveAcrossACallIsListed()
	{
		llvm::LLVMContext context;
		std::unique_ptr<llvm::Module> module = Parsed(context, R"(
			@g = global ptr null
			declare ptr @make()
			declare void @call()
			declare void @use(ptr, i64)
			define void @f(ptr %a, i64 %n) {
			entry:
				%slot = alloca ptr
				%field = getelementptr i8, ptr %a, i64 8
				%made = call ptr @make()
				%dead = call ptr @make()
				%bits = ptrtoint ptr %made to i64
				%local = getelementptr i8, ptr %slot, i64 8
				call void @call()
				call void @use(ptr %field, i64 %bits)
				call void @use(ptr %a, i64 %n)
				call void @use(ptr %local, i64 0)
				call void @use(ptr @g, i64 0)
				ret void
			}
		)");
		TEST_ASSERT(module != nullptr);
		if (!module) return;
		// %field points into %a, which is listed; %made lives on only as %bits; %dead is not read again; %local is a stack
		// slot's address, @g a global's.
		const std::vector<std::string> live = LiveAcross(*module->getFunction("f"), "call");
		TEST_ASSERT(live == std::vector<std::string>({ "a", "n", "bits" }));
	}

	void StackMapsTest::ACounterIsNoAddress()
	{
		llvm::LLVMContext context;
		std::unique_ptr<llvm::Module> module = Parsed(context, R"(
			declare void @call()
			declare void @use(ptr, i64)
			define void @f(ptr %p, i32 %count, ptr %q) {
			entry:
				%start = load i64, ptr %q
				br label %body
			body:
				%i = phi i64 [ 0, %entry ], [ %next, %body ]
				%address = phi i64 [ %start, %entry ], [ %step, %body ]
				call void @call()
				%next = add i64 %i, 1
				%step = add i64 %address, 16
				%wide = zext i32 %count to i64
				%done = icmp eq i64 %next, %wide
				br i1 %done, label %exit, label %body
			exit:
				call void @use(ptr %p, i64 %step)
				ret void
			}
		)");
		TEST_ASSERT(module != nullptr);
		if (!module) return;
		// The counter starts at a constant and only adds to itself; the integer loaded and stepped may be an address.
		const std::vector<std::string> live = LiveAcross(*module->getFunction("f"), "call");
		TEST_ASSERT(live == std::vector<std::string>({ "p", "address" }));
	}

	void StackMapsTest::OnlyASiblingCallKeepsNoRecord()
	{
		llvm::LLVMContext context;
		std::unique_ptr<llvm::Module> module = Parsed(context, R"(
			declare void @collect()
			declare ptr @make()
			define i32 @returnsZero() {
				tail call void @collect()
				ret i32 0
			}
			define i64 @returnsItsAddress() {
				%made = tail call ptr @make()
				%address = ptrtoint ptr %made to i64
				ret i64 %address
			}
			define ptr @returnsAfterABranch(i1 %which) {
			entry:
				br i1 %which, label %call, label %exit
			call:
				%made = tail call ptr @make()
				br label %exit
			exit:
				%result = phi ptr [ %made, %call ], [ null, %entry ]
				ret ptr %result
			}
		)");
		TEST_ASSERT(module != nullptr);
		if (!module) return;
		InsertStatepoints(*module);
		TEST_ASSERT(Verified(*module));
		// A call before a return of something else is no sibling call: its frame waits in it, and is read by its record.
		TEST_ASSERT(RecordedCalls(*module->getFunction("returnsZero"), "collect").size() == 1);
		TEST_ASSERT(RecordedCalls(*module->getFunction("returnsItsAddress"), "make").empty());
		TEST_ASSERT(RecordedCalls(*module->getFunction("returnsAfterABranch"), "make").empty());
	}

	void StackMapsTest::AnInvokeGetsARecord()
	{
		constexpr const char* kGuarded = R"(
			int32 g_done = 0;
			struct Guard { public int32 v; [operator("delete")] void Done() { g_done++; } }
			void MayThrow(int32 x) { if (x > 5) throw x; }
			int32 Guarded(int32 x) { Guard g; MayThrow(x); return g.v; }
			int32 main() { return Guarded(1); }
		)";
		for (const TargetSystem system : { TargetSystem::Windows, TargetSystem::Linux })
		{
			Lowered mapped = Lower(kGuarded, true, system);
			TEST_ASSERT(mapped.module != nullptr);
			if (!mapped.module) continue;
			InsertStatepoints(*mapped.module);
			TEST_ASSERT(Verified(*mapped.module));
			const llvm::Function* guarded = FunctionNamed(*mapped.module, "Guarded");
			TEST_ASSERT(guarded != nullptr);
			if (!guarded) continue;
			size_t invokes = 0;
			for (const llvm::Instruction& instruction : llvm::instructions(*guarded))
				if (const auto* invoke = llvm::dyn_cast<llvm::InvokeInst>(&instruction))
				{
					++invokes;
					TEST_ASSERT(invoke->getOperandBundle(llvm::LLVMContext::OB_deopt).has_value());
				}
			TEST_ASSERT(invokes >= 1);
			TEST_ASSERT(ObjectWritten(*mapped.module, system, 0));
		}
	}

	void StackMapsTest::ManyValuesAtOneCallFitTheFrame()
	{
		// Twelve fields of one object and twelve pointers loaded from them, all read after the call: code generation could
		// make each field's address again at the call from the object's, which wants a register apiece there.
		std::string body = "declare void @call()\ndeclare void @use(ptr)\ndefine void @f(ptr %a) {\n";
		for (int i = 0; i < 12; ++i)
			body += "%f" + std::to_string(i) + " = getelementptr i8, ptr %a, i64 " + std::to_string(8 * i + 16) + "\n%p" + std::to_string(i) +
				" = load ptr, ptr %f" + std::to_string(i) + "\n";
		body += "call void @call()\n";
		for (int i = 0; i < 12; ++i) body += "call void @use(ptr %f" + std::to_string(i) + ")\ncall void @use(ptr %p" + std::to_string(i) + ")\n";
		body += "ret void\n}\n";
		for (const TargetSystem system : { TargetSystem::Windows, TargetSystem::Linux })
		{
			llvm::LLVMContext context;
			std::unique_ptr<llvm::Module> module = Parsed(context, body, system);
			TEST_ASSERT(module != nullptr);
			if (module) TEST_ASSERT(ObjectWritten(*module, system, 2));
		}
	}

	void StackMapsTest::AnElfStackMapIsWritableAndBracketed()
	{
		Lowered mapped = Lower(kProgram, true, TargetSystem::Linux);
		TEST_ASSERT(mapped.module != nullptr);
		if (!mapped.module) return;
		const CpuTarget target = CpuTarget().On(TargetSystem::Linux);
		const ObjectSettings settings{ 0, &target, 1, false };
		const std::string path = (std::filesystem::temp_directory_path() / "c4_stack_maps_test.o").string();
		std::vector<ObjectDiagnostic> diagnostics;
		TEST_ASSERT(WriteObject(*mapped.module, settings, path, diagnostics));
		llvm::Expected<llvm::object::OwningBinary<llvm::object::ObjectFile>> object = llvm::object::ObjectFile::createObjectFile(path);
		TEST_ASSERT(static_cast<bool>(object));
		if (!object)
		{
			llvm::consumeError(object.takeError());
			return;
		}
		bool found = false;
		for (const llvm::object::SectionRef& section : object->getBinary()->sections())
		{
			llvm::Expected<llvm::StringRef> name = section.getName();
			if (!name || *name != "llvm_stackmaps")
			{
				if (!name) llvm::consumeError(name.takeError());
				continue;
			}
			found = true;
			const uint64_t flags = llvm::object::ELFSectionRef(section).getFlags();
			TEST_ASSERT((flags & llvm::ELF::SHF_WRITE) != 0);
			TEST_ASSERT((flags & llvm::ELF::SHF_GNU_RETAIN) != 0);
			TEST_ASSERT(section.getSize() > 0);
		}
		TEST_ASSERT(found);
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}

	void StackMapsTest::ALibraryCallCodeGenerationExpandsHasNoRecord()
	{
		// Optimized, a memcmp compared with 0 becomes bcmp where the C library has one (Linux), and code generation expands
		// it in place; a record would hide the call from it.
		Lowered mapped = Lower(R"(
			struct T { public int64 v; }
			uint64 g_size = 8;
			int32 Same(T* a, T* b) {
				if (memcmp((void*)a, (void*)b, g_size) == 0) return 1;
				return 0;
			}
			int32 main() { T* a = new T; T* b = new T; return Same(a, b); }
		)", true, TargetSystem::Linux);
		TEST_ASSERT(mapped.module != nullptr);
		if (!mapped.module) return;
		const CpuTarget target = CpuTarget().On(TargetSystem::Linux);
		const ObjectSettings settings{ 2, &target, 1, false };
		std::vector<ObjectDiagnostic> diagnostics;
		TEST_ASSERT(OptimizeModule(*mapped.module, settings, diagnostics));
		InsertStatepoints(*mapped.module);
		TEST_ASSERT(Verified(*mapped.module));
		size_t compares = 0;
		for (const llvm::Function& fn : *mapped.module)
			for (const llvm::Instruction& instruction : llvm::instructions(fn))
				if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction))
					if (const llvm::Function* callee = call->getCalledFunction(); callee && (callee->getName() == "bcmp" || callee->getName() == "memcmp"))
					{
						++compares;
						TEST_ASSERT(!call->getOperandBundle(llvm::LLVMContext::OB_deopt));
					}
		TEST_ASSERT(compares >= 1);
		TEST_ASSERT(ObjectWritten(*mapped.module, TargetSystem::Linux, 2));
	}

	void StackMapsTest::OnlyAProgramWithStackMapsLinksTheirRuntime()
	{
		TEST_ASSERT(RuntimeSourceIsForStackMaps("GC/GCStackMaps.c"));
		TEST_ASSERT(RuntimeSourceIsForStackMaps("GC/GCStackWalkWin32.c"));
		TEST_ASSERT(RuntimeSourceIsForStackMaps("GC/GCStackWalkPosix.c"));
		TEST_ASSERT(!RuntimeSourceIsForStackMaps("GC/GCPlatformWin32.c"));
		TEST_ASSERT(!RuntimeSourceIsForStackMaps("GC/GCThreads.c"));
		const TargetAbi& windows = TargetAbi::For(TargetSystem::Windows);
		auto defines = [](const std::vector<std::string>& command) {
			return std::find(command.begin(), command.end(), std::string("-DC4_STACK_MAPS=1")) != command.end();
		};
		TEST_ASSERT(defines(RuntimeCompileCommand("clang", "runtime", "runtime/GC/GCThreads.c", "GCThreads.obj", windows, "", true)));
		TEST_ASSERT(!defines(RuntimeCompileCommand("clang", "runtime", "runtime/GC/GCThreads.c", "GCThreads.obj", windows, "")));
	}
} // namespace Testing
