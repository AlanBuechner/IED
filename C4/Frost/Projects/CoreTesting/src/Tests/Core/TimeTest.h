#pragma once

#include "UnitTesting/TestBase.h"
#include "Core/Time.h"

namespace Testing
{
	CLASS(GROUP = Testing) TimeTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::TimeTest);

		TEST_FUNC(Time)
		void TestGetTime();

		TEST_FUNC(Time)
		void TestDeltaTime();
	};
}
