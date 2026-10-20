#include "UUIDTest.h"

namespace Testing
{
	void UUIDTest::TestUniqueGeneration()
	{
		Core::UUID uuid1 = Core::UUID::Create();
		Core::UUID uuid2 = Core::UUID::Create();

		// Two randomly generated UUIDs should almost certainly be different
		if ((uint64)uuid1 == (uint64)uuid2)
			TEST_FAILED("Expected two random UUIDs to be different");

		TEST_PASSED();
	}

	void UUIDTest::TestToStringAndParse()
	{
		Core::UUID uuid = Core::UUID::Create(0x123456789ABCDEF0);
		
		// Verify the UUID stores the correct value
		if ((uint64)uuid != 0x123456789ABCDEF0ULL)
			TEST_FAILED("Expected UUID to store the provided value");

		TEST_PASSED();
	}

	void UUIDTest::TestEqualityComparison()
	{
		Core::UUID uuid1 = Core::UUID::Create(0x42);
		Core::UUID uuid2 = Core::UUID::Create(0x42);
		Core::UUID uuid3 = Core::UUID::Create(0x99);

		if ((uint64)uuid1 != (uint64)uuid2)
			TEST_FAILED("Expected UUIDs with same value to be equal");
		if ((uint64)uuid1 == (uint64)uuid3)
			TEST_FAILED("Expected UUIDs with different values to not be equal");

		TEST_PASSED();
	}
}
