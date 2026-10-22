#include "DynamicBitAllocatorTest.h"

namespace Testing
{
	void DynamicBitAllocatorTest::TestAllocateSingle()
	{
		Core::DynamicBitAllocator allocator;
		uint64 index = allocator.AllocateIndex();

		if (index != 0)
		{
			Core::String msg = Core::FormatString("Expected first allocation to return index 0, got {0}", index);
			TEST_FAILED(msg);
		}
		if (!allocator.HasAllocation(index))
			TEST_FAILED("Expected allocated index to be marked as allocated");
		if (allocator.GetNumPages() == 0)
			TEST_FAILED("Expected at least one page after allocation");

		TEST_PASSED();
	}

	void DynamicBitAllocatorTest::TestFreeSlot()
	{
		Core::DynamicBitAllocator allocator;
		uint64 index = allocator.AllocateIndex();

		allocator.Free((uint32)index);
		if (allocator.HasAllocation(index))
			TEST_FAILED("Expected freed index to not be allocated");

		TEST_PASSED();
	}

	void DynamicBitAllocatorTest::TestResizeUp()
	{
		Core::DynamicBitAllocator allocator;

		// Allocate enough to force a resize beyond the first page (64 bits)
		for (uint32 i = 0; i < 70; i++)
			allocator.AllocateIndex();

		if (allocator.GetNumPages() < 2)
		{
			Core::String msg = Core::FormatString("Expected at least 2 pages after allocating 70 indices, got {0}", allocator.GetNumPages());
			TEST_FAILED(msg);
		}

		// Verify all are allocated
		for (uint32 i = 0; i < 70; i++)
		{
			if (!allocator.HasAllocation(i))
			{
				Core::String msg = Core::FormatString("Expected index {0} to be allocated", i);
				TEST_FAILED(msg);
			}
		}

		TEST_PASSED();
	}
}
