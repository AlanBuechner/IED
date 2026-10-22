#include "HashTest.h"

namespace Testing
{
	void HashTest::TestConsistentHash()
	{
		// Define dummy constants so COMPILE_TIME_CRC32_STR can use sizeof on them
		constexpr char hello[] = "hello";
		
		// Compile-time hash should be consistent across calls
		uint32 hash1 = COMPILE_TIME_CRC32_STR(hello);
		uint32 hash2 = COMPILE_TIME_CRC32_STR(hello);

		if (hash1 != hash2)
			TEST_FAILED("Expected consistent hash for same string");

		TEST_PASSED();
	}

	void HashTest::TestDifferentStrings()
	{
		constexpr char hello[] = "hello";
		constexpr char world[] = "world";

		uint32 hash1 = COMPILE_TIME_CRC32_STR(hello);
		uint32 hash2 = COMPILE_TIME_CRC32_STR(world);

		if (hash1 == hash2)
			TEST_FAILED("Expected different hashes for different strings");

		TEST_PASSED();
	}

	void HashTest::TestIntegerHash()
	{
		// Test that the CRC32 table-based hashing produces non-zero results
		constexpr char test[] = "test";
		uint32 hash = COMPILE_TIME_CRC32_STR(test);

		if (hash == 0)
			TEST_FAILED("Expected non-zero hash, got 0");
		if (hash == 0xFFFFFFFF)
			TEST_FAILED("Expected hash not to be all 1s");

		TEST_PASSED();
	}
}
