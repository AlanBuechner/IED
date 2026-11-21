#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Backend/MIR/MirReach (B10, B15, D-55, D-39): which bodies can run off their end, on the MIR built from hand-made
	// HIR, and which cases run into the next one.
	CLASS(GROUP = Testing) MirReachTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::MirReachTest);

		TEST_FUNC(Mir.Reach)
		void JumpsEndABlock();
		TEST_FUNC(Mir.Reach)
		void BranchesCompleteWhenEitherDoes();
		TEST_FUNC(Mir.Reach)
		void LoopsEndThroughTheirConditionOrABreak();
		TEST_FUNC(Mir.Reach)
		void SwitchesAndTries();
		TEST_FUNC(Mir.Reach)
		void ALabelRevivesTheCodeAfterIt();
		TEST_FUNC(Mir.Reach)
		void ACaseThatCanEndRunsIntoTheNext();
		TEST_FUNC(Mir.Reach)
		void ANestedTryCatchesWhatTheInnerOneLetsGo();
	};
} // namespace Testing
