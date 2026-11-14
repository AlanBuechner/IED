#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Core/Core.h"

namespace Testing
{
	// Core's Platform calls (Core/Platform/PlatformCommon.h), each system's own: Windows' in Platform/Windows/Win.cpp,
	// Linux's in Platform/Linux/Linux.cpp.
	CLASS(GROUP = Testing) PlatformTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::PlatformTest);

		TEST_FUNC(Platform)
		void Utf8RoundTripsThroughWideText();

		TEST_FUNC(Platform)
		void InvalidUtf8BecomesReplacementCharacters();

		TEST_FUNC(Platform)
		void NullTextIsEmpty();

		TEST_FUNC(Platform)
		void ApplicationPathIsThisProgram();

		// This program run again with -list-tests=1: what it prints, and its exit code.
		TEST_FUNC(Platform)
		void ExecuteProcessCapturesWhatTheProgramPrints();

		TEST_FUNC(Platform)
		void SystemCallReturnsTheCommandsExitCode();
	};
}
