#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Debug sessions of real programs built with C4.exe -g: breakpoints bind and stop with the right stack, each
	// step lands on the right line, the program's output and exit code reach the client, and pause and terminate
	// work on a running program.
	CLASS(GROUP = Testing) DapSessionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DapSessionTest);

		TEST_FUNC_TIMEOUT(Dap.Session, 60)
		void ABreakpointStopsWithTheStackAndLocals();
		TEST_FUNC_TIMEOUT(Dap.Session, 60)
		void StepsLandOnTheRightLines();
		TEST_FUNC_TIMEOUT(Dap.Session, 60)
		void ABreakpointInsideAStepOverStops();
		TEST_FUNC_TIMEOUT(Dap.Session, 60)
		void ARecursiveCallDoesNotEndAStepEarly();
		TEST_FUNC_TIMEOUT(Dap.Session, 60)
		void AStepOverAThrowStopsInTheCatch();
		TEST_FUNC_TIMEOUT(Dap.Session, 60)
		void ALineWithoutCodeBindsToTheNextLine();
		TEST_FUNC_TIMEOUT(Dap.Session, 60)
		void ATemplateBreakpointStopsInEveryInstance();
		TEST_FUNC_TIMEOUT(Dap.Session, 60)
		void ALambdaBodyStops();
		TEST_FUNC_TIMEOUT(Dap.Session, 60)
		void StopOnEntryStopsAtMain();
		TEST_FUNC_TIMEOUT(Dap.Session, 60)
		void OutputAndTheExitCodeReachTheClient();
		TEST_FUNC_TIMEOUT(Dap.Session, 60)
		void PauseBreaksInAndTerminateEnds();
	};
} // namespace Testing
