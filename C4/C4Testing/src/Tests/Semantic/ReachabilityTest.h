#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Backend/HIR/Reachability (B10, B15): which HIR statement lists can run off their end.
	CLASS(GROUP = Testing) ReachabilityTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ReachabilityTest);

		TEST_FUNC(Semantic.Reachability)
		void JumpsEndABlock();
		TEST_FUNC(Semantic.Reachability)
		void BranchesCompleteWhenEitherDoes();
		TEST_FUNC(Semantic.Reachability)
		void LoopsEndThroughTheirConditionOrABreak();
		TEST_FUNC(Semantic.Reachability)
		void SwitchesAndTries();
		TEST_FUNC(Semantic.Reachability)
		void ALabelRevivesTheCodeAfterIt();
	};
} // namespace Testing
