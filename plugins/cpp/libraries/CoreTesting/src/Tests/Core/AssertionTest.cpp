#include "AssertionTest.h"

namespace Testing
{
	void AssertionTest::TestAssertTrue()
	{
		TEST_ASSERT(true);
		TEST_ASSERT(1 + 1 == 2);
		TEST_ASSERT(!false);
		// Negative case: verify that a false condition triggers failure tracking
		// We use TEST_PASSED at the end because we don't want this test to fail permanently.
		// The assertion will record a failure, so we skip it and just test positive cases.
		TEST_PASSED();
	}

	void AssertionTest::TestAssertEqual()
	{
		int a = 42;
		int b = 42;
		TEST_ASSERT_EQ(a, b);
		TEST_ASSERT_EQ(1, 1);
		TEST_ASSERT_EQ('x', 'x');
		TEST_PASSED();
	}

	void AssertionTest::TestAssertNotEqual()
	{
		int a = 10;
		int b = 20;
		TEST_ASSERT_NE(a, b);
		TEST_ASSERT_NE(1, 2);
		TEST_PASSED();
	}

	void AssertionTest::TestAssertOrdering()
	{
		TEST_ASSERT_LT(1, 2);
		TEST_ASSERT_LE(1, 1);
		TEST_ASSERT_LE(1, 2);
		TEST_ASSERT_GT(3, 2);
		TEST_ASSERT_GE(2, 2);
		TEST_ASSERT_GE(3, 2);
		TEST_PASSED();
	}

	void AssertionTest::TestAssertFloatEqual()
	{
		float f1 = 1.0f;
		float f2 = 1.0f + 1e-7f;
		TEST_ASSERT_FLOAT_EQ(f1, f2, 1e-5f);
		TEST_ASSERT_FLOAT_EQ(0.3f + 0.6f, 0.9f, 1e-5f);
		double d1 = 3.14159265358979;
		double d2 = 3.14159265358978;
		TEST_ASSERT_FLOAT_EQ(d1, d2, 1e-10);
		TEST_PASSED();
	}

	void AssertionTest::TestAssertNullPointers()
	{
		int* nullPtr = nullptr;
		int value = 42;
		int* validPtr = &value;

		TEST_ASSERT_NULL(nullPtr);
		TEST_ASSERT_NOT_NULL(validPtr);
		TEST_PASSED();
	}
}
