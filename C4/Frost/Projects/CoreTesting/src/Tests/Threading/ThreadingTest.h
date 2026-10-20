#pragma once

#include "UnitTesting/TestBase.h"
#include "Threading/JobSystem.h"
#include "Threading/Flag.h"

namespace Testing
{
	CLASS(GROUP = Testing) ThreadingTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ThreadingTest);

		TEST_FUNC(Threading)
		void TestJobCompletion();

		TEST_FUNC(Threading)
		void TestFlagSetAndWait();

		TEST_FUNC(Threading)
		void TestThreadCountPositive();
	};
}
