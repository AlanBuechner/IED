#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Backend/MIR/MirInterpreter (R6, roadmap 6): a function's MIR run at compile time over folded arguments - scalars and
	// value types (stage 1), control flow and bounded loops (stage 2), arrays and aggregates (stage 3) - and the errors
	// for what compile time cannot do, the step limit and the recursion limit.
	CLASS(GROUP = Testing) MirInterpreterTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::MirInterpreterTest);

		TEST_FUNC(Mir.Interpreter)
		void ScalarsFold();
		TEST_FUNC(Mir.Interpreter)
		void ArithmeticWrapsAsAtRunTime();
		TEST_FUNC(Mir.Interpreter)
		void ValueTypesAndMethods();
		TEST_FUNC(Mir.Interpreter)
		void LoopsBranchesAndSwitches();
		TEST_FUNC(Mir.Interpreter)
		void RecursionRuns();
		TEST_FUNC(Mir.Interpreter)
		void ArraysBuildTables();
		TEST_FUNC(Mir.Interpreter)
		void ALoopThatDoesNotEndHitsTheStepLimit();
		TEST_FUNC(Mir.Interpreter)
		void RecursionThatDoesNotEndHitsTheDepthLimit();
		TEST_FUNC(Mir.Interpreter)
		void WhatCompileTimeCannotDoIsAnError();
	};
} // namespace Testing
