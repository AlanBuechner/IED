#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The debug adapter's stepping decisions, from what the thread looks like between two instructions: a call, a
	// return, the line changing, code without lines.
	CLASS(GROUP = Testing) DapStepTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DapStepTest);

		TEST_FUNC(Dap.Step)
		void OverAndIntoSingleStepOutRunsToTheCaller();
		TEST_FUNC(Dap.Step)
		void TheSameLineKeepsStepping();
		TEST_FUNC(Dap.Step)
		void ANewLineStops();
		TEST_FUNC(Dap.Step)
		void OverRunsACallToItsReturn();
		TEST_FUNC(Dap.Step)
		void IntoStopsInACalleeWithLines();
		TEST_FUNC(Dap.Step)
		void IntoRunsThroughACalleeWithoutLines();
		TEST_FUNC(Dap.Step)
		void ReturningStopsInTheCaller();
		TEST_FUNC(Dap.Step)
		void ReturningIntoCodeWithoutLinesRuns();
		TEST_FUNC(Dap.Step)
		void APushIsNotACall();
		TEST_FUNC(Dap.Step)
		void AfterTheReturnBreakpoint();
	};
} // namespace Testing
