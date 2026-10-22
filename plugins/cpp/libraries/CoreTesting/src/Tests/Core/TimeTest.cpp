#include "TimeTest.h"

namespace Testing
{
	void TimeTest::TestGetTime()
	{
		double time = Core::Time::GetTime();
		if (time < 0.0)
			TEST_FAILED("Expected positive time value");

		TEST_PASSED();
	}

	void TimeTest::TestDeltaTime()
	{
		// Update delta time and verify it's non-negative
		Core::Time::UpdateDeltaTime();
		double dt = Core::Time::GetDeltaTime();
		if (dt < 0.0)
			TEST_FAILED("Expected non-negative delta time");

		TEST_PASSED();
	}
}
