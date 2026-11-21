#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Backend/MIR/MirBuilder and MirMoves (R5): the control-flow graph built from a lowered body - scope exits running the
	// cleanups they leave, returns sharing their way out, unwind steps, try dispatches and handlers, regions, once regions,
	// a noexcept body's catch-all, drop flags.
	CLASS(GROUP = Testing) MirBuilderTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::MirBuilderTest);

		TEST_FUNC(Mir.Builder)
		void AJumpRunsTheCleanupsItLeaves();
		TEST_FUNC(Mir.Builder)
		void ReturnsShareTheirWayOut();
		TEST_FUNC(Mir.Builder)
		void ACallUnwindsThroughTheCleanupsAroundIt();
		TEST_FUNC(Mir.Builder)
		void ATryDispatchesToItsHandlers();
		TEST_FUNC(Mir.Builder)
		void AnExpressionRunsItsBlocksAsRegions();
		TEST_FUNC(Mir.Builder)
		void ANoexceptBodyRunsInACatchAll();
		TEST_FUNC(Mir.Builder)
		void AStaticLocalStartsOnce();
		TEST_FUNC(Mir.Builder)
		void AMovedVariablesCleanupTestsItsFlag();
		TEST_FUNC(Mir.Builder)
		void AnIncompleteBodyHasNoMir();
	};
} // namespace Testing
