#include "StackMapsTest.h"
#include "Backend/ProgramBackend.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/ObjectEmission.h"
#include "Compiler/RuntimeObjects.h"
#include "Compiler/StackMaps.h"
#include "Compiler/Toolchain.h"
#include <llvm/BinaryFormat/ELF.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Object/ELFObjectFile.h>
#include <llvm/Object/ObjectFile.h>
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

		const llvm::Function* StatepointCallee(const llvm::CallBase& call)
		{
			const llvm::Function* callee = call.getCalledFunction();
			if (!callee || callee->getIntrinsicID() != llvm::Intrinsic::experimental_gc_statepoint) return nullptr;
			return llvm::dyn_cast<llvm::Function>(call.getArgOperand(2)->stripPointerCasts());
		}

		// The statepoints of `fn` whose callee's name starts with `prefix`.
		std::vector<const llvm::CallBase*> StatepointsCalling(const llvm::Function& fn, const char* prefix)
		{
			std::vector<const llvm::CallBase*> found;
			for (const llvm::Instruction& instruction : llvm::instructions(fn))
				if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction))
					if (const llvm::Function* callee = StatepointCallee(*call); callee && callee->getName().starts_with(prefix)) found.push_back(call);
			return found;
		}

		// How many stack slots a statepoint lists (Compiler/StackMapSlots: each slot, then its words and masks).
		size_t ListedSlots(const llvm::CallBase& statepoint)
		{
			const std::optional<llvm::OperandBundleUse> deopt = statepoint.getOperandBundle(llvm::LLVMContext::OB_deopt);
			size_t slots = 0;
			if (deopt)
				for (const llvm::Use& input : deopt->Inputs) slots += llvm::isa<llvm::AllocaInst>(input.get()) ? 1 : 0;
			return slots;
		}
	}

	void StackMapsTest::WithoutTheFlagNothingChanges()
	{
		Lowered plain = Lower(kProgram, false);
		TEST_ASSERT(plain.module != nullptr);
		if (!plain.module) return;
		TEST_ASSERT(plain.ir.find("addrspace(1)") == std::string::npos);
		TEST_ASSERT(!UsesStackMaps(*plain.module));
		InsertStatepoints(*plain.module, nullptr);
		TEST_ASSERT(Text(*plain.module) == plain.ir);
	}

	void StackMapsTest::ManagedPointersHaveTheirOwnSpace()
	{
		Lowered mapped = Lower(kProgram, true);
		TEST_ASSERT(mapped.module != nullptr);
		if (!mapped.module) return;
		TEST_ASSERT(UsesStackMaps(*mapped.module));
		TEST_ASSERT(mapped.module->getDataLayout().isNonIntegralAddressSpace(c_ManagedAddressSpace));
		const llvm::Function* use = FunctionNamed(*mapped.module, "Use");
		TEST_ASSERT(use && use->arg_size() == 1 && use->getArg(0)->getType()->getPointerAddressSpace() == c_ManagedAddressSpace);
		TEST_ASSERT(Verified(*mapped.module));
	}

	void StackMapsTest::CallsBecomeStatepoints()
	{
		Lowered mapped = Lower(kProgram, true);
		TEST_ASSERT(mapped.module != nullptr);
		if (!mapped.module) return;
		InsertStatepoints(*mapped.module, nullptr);
		TEST_ASSERT(Verified(*mapped.module));
		const llvm::Function* slots = FunctionNamed(*mapped.module, "Slots");
		TEST_ASSERT(slots && slots->hasGC() && slots->getGC() == c_StackMapsGc);
		if (slots) TEST_ASSERT(StatepointsCalling(*slots, "Call").size() == 1);
	}

	void StackMapsTest::ADeadSlotIsNotListed()
	{
		Lowered mapped = Lower(kProgram, true);
		TEST_ASSERT(mapped.module != nullptr);
		if (!mapped.module) return;
		InsertStatepoints(*mapped.module, nullptr);
		const llvm::Function* slots = FunctionNamed(*mapped.module, "Slots");
		TEST_ASSERT(slots != nullptr);
		if (!slots) return;
		const std::vector<const llvm::CallBase*> calls = StatepointsCalling(*slots, "Call");
		const std::vector<const llvm::CallBase*> allocations = StatepointsCalling(*slots, "c4_alloc");
		TEST_ASSERT(calls.size() == 1 && allocations.size() == 3);
		if (calls.size() != 1 || allocations.size() != 3) return;
		// At the third `new` a and b are read later; at Call only c is.
		TEST_ASSERT(ListedSlots(*allocations[2]) >= 2);
		TEST_ASSERT(ListedSlots(*calls[0]) >= 1);
		TEST_ASSERT(ListedSlots(*calls[0]) < ListedSlots(*allocations[2]));
	}

	void StackMapsTest::InvokesStayLeaves()
	{
		Lowered mapped = Lower(R"(
			int32 g_done = 0;
			struct Guard { public int32 v; [operator("delete")] void Done() { g_done++; } }
			void MayThrow(int32 x) { if (x > 5) throw x; }
			int32 Guarded(int32 x) { Guard g; MayThrow(x); return g.v; }
			int32 main() { return Guarded(1); }
		)", true);
		TEST_ASSERT(mapped.module != nullptr);
		if (!mapped.module) return;
		InsertStatepoints(*mapped.module, nullptr);
		TEST_ASSERT(Verified(*mapped.module));
		const llvm::Function* guarded = FunctionNamed(*mapped.module, "Guarded");
		TEST_ASSERT(guarded != nullptr);
		if (!guarded) return;
		size_t invokes = 0;
		for (const llvm::Instruction& instruction : llvm::instructions(*guarded))
			if (const auto* invoke = llvm::dyn_cast<llvm::InvokeInst>(&instruction))
			{
				++invokes;
				TEST_ASSERT(invoke->hasFnAttr("gc-leaf-function"));
			}
		TEST_ASSERT(invokes >= 1);
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

	void StackMapsTest::ALibraryCallKeepsItsManagedPointers()
	{
		// Optimized, a memcmp compared with 0 becomes bcmp where the C library has one (Linux), declared with raw pointers.
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
		TEST_ASSERT(Verified(*mapped.module));
		TEST_ASSERT(mapped.module->getFunction("bcmp") == nullptr);
		const llvm::Function* memcmp = mapped.module->getFunction("memcmp");
		TEST_ASSERT(memcmp && memcmp->hasFnAttribute(llvm::Attribute::NoBuiltin));
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
