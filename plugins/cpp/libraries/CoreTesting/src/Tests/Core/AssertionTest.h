#pragma once

#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	CLASS(GROUP = Testing) AssertionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::AssertionTest);

		TEST_FUNC(Core.Assertions.Basic)
		void TestAssertTrue();

		TEST_FUNC(Core.Assertions.Equality)
		void TestAssertEqual();

		TEST_FUNC(Core.Assertions.Inequality)
		void TestAssertNotEqual();

		TEST_FUNC(Core.Assertions.Ordering)
		void TestAssertOrdering();

		TEST_FUNC(Core.Assertions.Floats)
		void TestAssertFloatEqual();

		TEST_FUNC(Core.Assertions.Pointers)
		void TestAssertNullPointers();
	};
}
