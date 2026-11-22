#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"
#include "Backend/HIR/HIR.h"

namespace Testing
{
	// LOWERING ON SEVERAL THREADS (ParallelLowering): each piece run on several threads at once - the scope cursors,
	// the type store's interning, the worker pool - the model's changes as requests (made once, shown when the outermost
	// request ends, a change outside one counted as a violation), and whole programs lowered on one thread and on eight,
	// which must build the same module, name the same records alike and report the same diagnostics in the same order.
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
		void ARequestIsShownWhenItsOutermostRequestEnds();

		TEST_FUNC(Semantic.ParallelLowering)
		void AListAddedToInARequestIsShownInItsOrder();

		TEST_FUNC(Semantic.ParallelLowering)
		void AChangeOutsideARequestIsAViolationWhileLoweringsShare();

		TEST_FUNC(Semantic.ParallelLowering)
		void EveryLoweringThatAsksFindsTheOneInstance();

		TEST_FUNC(Semantic.ParallelLowering)
		void ALoweringMakesItsClosuresInItsOrder();

		TEST_FUNC(Semantic.ParallelLowering)
		void RecordsMadeOnThreadsAreNamedInOneOrder();

		TEST_FUNC(Semantic.ParallelLowering)
		void LoweringOnEightThreadsBuildsTheSameModule();

		TEST_FUNC(Semantic.ParallelLowering)
		void ManyLoweringsAskForTheSameInstancesAtOnce();

		TEST_FUNC(Semantic.ParallelLowering)
		void DiagnosticsKeepTheOrderOfTheFunctions();
	};
}
