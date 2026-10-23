#include "UnwindInferenceTest.h"
#include "Backend/CodeGen/TargetAttributes.h"
#include "Backend/CodeGen/UnwindInference.h"
#include "Compiler/CpuTarget.h"
#include "Compiler/WorkerPool.h"
#include <llvm/AsmParser/Parser.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/SourceMgr.h>
#include <memory>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// Modules in contexts of their own, after InferNoUnwind ran over all of them on three threads.
		struct Program
		{
			std::vector<std::unique_ptr<llvm::LLVMContext>> contexts;
			std::vector<std::unique_ptr<llvm::Module>> modules;

			explicit Program(const std::vector<std::string>& texts)
			{
				std::vector<llvm::Module*> all;
				for (const std::string& text : texts)
				{
					contexts.push_back(std::make_unique<llvm::LLVMContext>());
					llvm::SMDiagnostic error;
					modules.push_back(llvm::parseAssemblyString(text, error, *contexts.back()));
					all.push_back(modules.back().get());
				}
				WorkerPool pool(3);
				InferNoUnwind(all, pool);
			}

			bool Parsed() const
			{
				for (const auto& module : modules)
					if (!module) return false;
				return true;
			}

			bool Verifies() const
			{
				for (const auto& module : modules)
					if (llvm::verifyModule(*module, &llvm::errs())) return false;
				return true;
			}

			bool NoUnwind(size_t module, const char* name) const { return modules[module]->getFunction(name)->doesNotThrow(); }
		};

		const char* kDefinitions =
			"declare void @_CxxThrowException(ptr, ptr)\n"
			"define void @leaf() {\n ret void\n}\n"
			"define void @cleanup() {\n ret void\n}\n"
			"define void @thrower() {\n call void @_CxxThrowException(ptr null, ptr null)\n ret void\n}\n";

		const char* kCallers =
			"declare void @leaf()\n"
			"declare void @thrower()\n"
			"declare void @cleanup()\n"
			"declare i32 @__CxxFrameHandler3(...)\n"
			"define void @caller() personality ptr @__CxxFrameHandler3 {\n"
			"entry:\n invoke void @leaf() to label %next unwind label %pad\n"
			"next:\n invoke void @thrower() to label %done unwind label %pad\n"
			"done:\n ret void\n"
			"pad:\n %p = cleanuppad within none []\n call void @cleanup() [ \"funclet\"(token %p) ]\n cleanupret from %p unwind to caller\n}\n"
			"define void @quiet() personality ptr @__CxxFrameHandler3 {\n"
			"entry:\n invoke void @leaf() to label %done unwind label %pad\n"
			"done:\n ret void\n"
			"pad:\n %p = cleanuppad within none []\n call void @cleanup() [ \"funclet\"(token %p) ]\n cleanupret from %p unwind to caller\n}\n";

		size_t Count(const llvm::Function& fn, unsigned opcode)
		{
			size_t count = 0;
			for (const llvm::Instruction& inst : llvm::instructions(fn))
				if (inst.getOpcode() == opcode) ++count;
			return count;
		}

		bool CallsAsInvoke(const llvm::Function& fn, const char* callee)
		{
			for (const llvm::Instruction& inst : llvm::instructions(fn))
				if (const auto* invoke = llvm::dyn_cast<llvm::InvokeInst>(&inst); invoke && invoke->getCalledFunction()->getName() == callee) return true;
			return false;
		}
	}

	void UnwindInferenceTest::InvokesOfFunctionsThatCannotUnwindBecomeCalls()
	{
		const Program program({ kDefinitions, kCallers });
		TEST_ASSERT(program.Parsed());
		TEST_ASSERT(program.NoUnwind(0, "leaf") && program.NoUnwind(0, "cleanup"));
		TEST_ASSERT(!program.NoUnwind(0, "thrower"));
		TEST_ASSERT(program.NoUnwind(1, "leaf")); // the declaration in the other module too
		const llvm::Function& caller = *program.modules[1]->getFunction("caller");
		TEST_ASSERT(!CallsAsInvoke(caller, "leaf"));
		TEST_ASSERT(CallsAsInvoke(caller, "thrower"));
		TEST_ASSERT(!program.NoUnwind(1, "caller"));
		TEST_ASSERT(program.Verifies());
	}

	void UnwindInferenceTest::ExceptionPathsNothingReachesAreDropped()
	{
		const Program program({ kDefinitions, kCallers });
		TEST_ASSERT(program.Parsed());
		const llvm::Function& quiet = *program.modules[1]->getFunction("quiet");
		TEST_ASSERT(program.NoUnwind(1, "quiet"));
		TEST_ASSERT_EQ(Count(quiet, llvm::Instruction::Invoke), size_t(0));
		TEST_ASSERT_EQ(Count(quiet, llvm::Instruction::CleanupPad), size_t(0));
		TEST_ASSERT_EQ(quiet.size(), size_t(1)); // the call continues in its own block
		const llvm::Function& caller = *program.modules[1]->getFunction("caller");
		TEST_ASSERT_EQ(Count(caller, llvm::Instruction::CleanupPad), size_t(1)); // thrower still reaches it
		TEST_ASSERT(program.Verifies());
	}

	void UnwindInferenceTest::RecursionWithoutThrowsCannotUnwind()
	{
		const Program program({ "declare void @pong()\ndefine void @ping() {\n call void @pong()\n ret void\n}\n",
			"declare void @ping()\ndefine void @pong() {\n call void @ping()\n ret void\n}\n" });
		TEST_ASSERT(program.Parsed());
		TEST_ASSERT(program.NoUnwind(0, "ping") && program.NoUnwind(1, "pong"));
		TEST_ASSERT(program.NoUnwind(0, "pong") && program.NoUnwind(1, "ping"));
	}

	void UnwindInferenceTest::CallsThroughPointersAndUnknownDeclarationsUnwind()
	{
		const Program program({
			"define void @through(ptr %f) {\n call void %f()\n ret void\n}\n"
			"define void @user() {\n call void @through(ptr null)\n ret void\n}\n"
			"declare void @foreign()\n"
			"declare void @promised() nounwind\n"
			"define void @outward() {\n call void @foreign()\n ret void\n}\n"
			"define void @kept() {\n call void @promised()\n ret void\n}\n" });
		TEST_ASSERT(program.Parsed());
		TEST_ASSERT(!program.NoUnwind(0, "through") && !program.NoUnwind(0, "user"));
		TEST_ASSERT(!program.NoUnwind(0, "outward"));
		TEST_ASSERT(program.NoUnwind(0, "kept"));
	}

	void UnwindInferenceTest::FunctionsThatCannotUnwindKeepTheirUnwindTables()
	{
		// A fault in a callee unwinds through them to a __try (a script host's guarded call): Windows x64 needs one
		// for every frame on the way.
		const Program program({ kDefinitions, kCallers });
		TEST_ASSERT(program.Parsed());
		for (const auto& module : program.modules) ApplyTarget(*module, CpuTarget());
		TEST_ASSERT(program.NoUnwind(0, "leaf") && program.NoUnwind(1, "quiet"));
		TEST_ASSERT(program.modules[0]->getFunction("leaf")->needsUnwindTableEntry());
		TEST_ASSERT(program.modules[1]->getFunction("quiet")->needsUnwindTableEntry());
	}

	void UnwindInferenceTest::LocalFunctionsAreTheirModulesOwn()
	{
		const Program program({
			"declare void @_CxxThrowException(ptr, ptr)\n"
			"define internal void @helper() {\n call void @_CxxThrowException(ptr null, ptr null)\n ret void\n}\n"
			"define void @a() {\n call void @helper()\n ret void\n}\n",
			"define internal void @helper() {\n ret void\n}\n"
			"define void @b() {\n call void @helper()\n ret void\n}\n" });
		TEST_ASSERT(program.Parsed());
		TEST_ASSERT(!program.NoUnwind(0, "helper") && !program.NoUnwind(0, "a"));
		TEST_ASSERT(program.NoUnwind(1, "helper") && program.NoUnwind(1, "b"));
	}
} // namespace Testing
