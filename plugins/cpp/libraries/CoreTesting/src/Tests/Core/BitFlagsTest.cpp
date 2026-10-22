#include "BitFlagsTest.h"

namespace Testing
{
	void BitFlagsTest::TestSetSingleFlag()
	{
		Core::BitFlags flags;
		flags.SetFlag(BIT(0));

		if (!flags.TestFlag(BIT(0)))
			TEST_FAILED("Expected bit 0 to be set");
		if (flags.TestFlag(BIT(1)))
			TEST_FAILED("Expected bit 1 to be unset");

		TEST_PASSED();
	}

	void BitFlagsTest::TestClearFlag()
	{
		Core::BitFlags flags;
		flags.SetFlag(BIT(0));
		flags.SetFlag(BIT(1));
		flags.ClearFlag(BIT(0));

		if (flags.TestFlag(BIT(0)))
			TEST_FAILED("Expected bit 0 to be cleared");
		if (!flags.TestFlag(BIT(1)))
			TEST_FAILED("Expected bit 1 to still be set");

		TEST_PASSED();
	}

	void BitFlagsTest::TestCheckFlag()
	{
		Core::BitFlags flags;
		flags.SetFlag(BIT(2));

		if (!flags.TestFlag(BIT(2)))
			TEST_FAILED("Expected bit 2 to be set");
		if (flags.TestFlag(BIT(3)))
			TEST_FAILED("Expected bit 3 to be unset");

		// Test conditional set
		flags.SetFlag(BIT(3), false);
		if (flags.TestFlag(BIT(3)))
			TEST_FAILED("Expected bit 3 to remain unset after SetFlag(false)");

		flags.SetFlag(BIT(3), true);
		if (!flags.TestFlag(BIT(3)))
			TEST_FAILED("Expected bit 3 to be set after SetFlag(true)");

		TEST_PASSED();
	}
}
