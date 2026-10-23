#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"
#include "Backend/HIR/HIR.h"

namespace Testing
{
	// LOWERING ON SEVERAL THREADS (ParallelLowering): each piece run on several threads at once - the scope cursors,
	// the type store's interning, the worker pool - the frozen model's refusals and the changes it lets be made between
	// rounds, and whole programs lowered on one thread and on eight, which must build the same module and report the
	// same diagnostics in the same order.
	CLASS(GROUP = Testing) ParallelLoweringTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ParallelLoweringTest);

		TEST_FUNC(Semantic.ParallelLowering)
		void ScopeCursorsOnSeveralThreadsKeepTheirLocals();

		TEST_FUNC(Semantic.ParallelLowering)
		void ALoweringInsideAnotherStartsWhereItIs();

		TEST_FUNC(Semantic.ParallelLowering)
		void InterningOnSeveralThreadsKeepsOneType();

		TEST_FUNC(Semantic.ParallelLowering)
		void WorkerPoolRunsEveryIndexOncePerRound();

		TEST_FUNC(Semantic.ParallelLowering)
		void ARunInsideARunRunsOnItsThread();

		TEST_FUNC(Semantic.ParallelLowering)
		void AFrozenModelRefusesEveryChange();

		TEST_FUNC(Semantic.ParallelLowering)
		void AStoppedLoweringNamesAnInstanceToMake();

		TEST_FUNC(Semantic.ParallelLowering)
		void ClosuresAreMadeOneARoundInTheirOrder();

		TEST_FUNC(Semantic.ParallelLowering)
		void LoweringOnEightThreadsBuildsTheSameModule();

		TEST_FUNC(Semantic.ParallelLowering)
		void DiagnosticsKeepTheOrderOfTheFunctions();
	};
}
