#include "ThreadingTest.h"

namespace Testing
{
	void ThreadingTest::TestJobCompletion()
	{
		// Test basic thread creation and joining
		std::atomic<int32> counter(0);
		std::thread t([&counter]() {
			counter++;
		});
		
		t.join();
		
		if (counter != 1)
			TEST_FAILED("Expected counter to be 1 after thread completion");

		TEST_PASSED();
	}

	void ThreadingTest::TestFlagSetAndWait()
	{
		Threading::Flag flag;
		
		// Test basic flag operations
		if (flag.GetFlag())
			TEST_FAILED("Expected flag to start as false");
		
		flag.Signal();
		if (!flag.GetFlag())
			TEST_FAILED("Expected flag to be true after Signal()");
		
		flag.Clear();
		if (flag.GetFlag())
			TEST_FAILED("Expected flag to be false after Clear()");

		TEST_PASSED();
	}

	void ThreadingTest::TestThreadCountPositive()
	{
		// Verify hardware concurrency is positive
		uint32 count = std::thread::hardware_concurrency();
		
		if (count == 0)
			TEST_FAILED("Expected positive thread count from hardware_concurrency");

		TEST_PASSED();
	}
}
