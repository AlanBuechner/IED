#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// The debug adapter's protocol side with no program running: capabilities, sequence numbers, and the errors a
	// client gets for an unknown request, a launch without a program, inspecting nothing.
	CLASS(GROUP = Testing) DapServerTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DapServerTest);

		TEST_FUNC(Dap.Server)
		void InitializeAnswersThenSaysInitialized();
		TEST_FUNC(Dap.Server)
		void AnUnknownRequestFails();
		TEST_FUNC(Dap.Server)
		void LaunchNeedsAProgramThatExists();
		TEST_FUNC(Dap.Server)
		void BreakpointsBeforeLaunchWaitForTheProgram();
		TEST_FUNC(Dap.Server)
		void SetBreakpointsNeedsASource();
		TEST_FUNC(Dap.Server)
		void InspectingWithoutAStopFindsNothing();
	};
} // namespace Testing
