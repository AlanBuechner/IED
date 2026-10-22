#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"
#include "Backend/CodeGen/CodeGen.h"

namespace Testing
{
	// CODEGEN (LLVM EMISSION) TESTS - drive the new HIR-driven emitter (docs/compiler/04-codegen-and-abi.md) in-process.
	// Each test lowers a small program to an llvm::Module and asserts it VERIFIES: the cheapest place to catch
	// an ABI/SSA/type bug before it becomes runtime corruption. The e2e suite (TestCode -O0/-O2) is the
	// integration oracle; these are the per-feature unit checks that localize a regression to one brick.
	CLASS(GROUP = Testing) CodeGenTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::CodeGenTest);

		TEST_FUNC(Semantic.CodeGen)
		void StraightLineEmitsVerifiedModule();

		TEST_FUNC(Semantic.CodeGen)
		void ControlFlowEmitsVerifiedModule();

		TEST_FUNC(Semantic.CodeGen)
		void StructReturnUsesSretAndVerifies();

		TEST_FUNC(Semantic.CodeGen)
		void MethodCallOnObjectEmitsVerifiedModule();

		TEST_FUNC(Semantic.CodeGen)
		void NestedFunctionsEmitAsSiblings();

		TEST_FUNC(Semantic.CodeGen)
		void MixedWidthOpsEmitVerifiedModule();

		TEST_FUNC(Semantic.CodeGen)
		void ModuleTargetsWindowsX64();
		TEST_FUNC(Semantic.CodeGen)
		void ATargetAnnotationWidensOneFunction();
		TEST_FUNC(Semantic.CodeGen)
		void HintsReachTheOptimizer();

		TEST_FUNC(Semantic.CodeGen)
		void PointerFreeObjectsAllocateNoscan();

		TEST_FUNC(Semantic.CodeGen)
		void GcRootsListEveryPointerHoldingGlobal();

		TEST_FUNC(Semantic.CodeGen)
		void DeclarationsOfOneForeignSymbolShareAFunction();

		TEST_FUNC(Semantic.CodeGen)
		void TestHarnessModuleIsComplete();
	};
} // namespace Testing
