#pragma once

#include "UnitTesting/TestBase.h"
#include "Core/DynamicBitAllocator.h"

namespace Testing
{
	CLASS(GROUP = Testing) DynamicBitAllocatorTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DynamicBitAllocatorTest);

		TEST_FUNC(DynamicBitAllocator)
		void TestAllocateSingle();

		TEST_FUNC(DynamicBitAllocator)
		void TestFreeSlot();

		TEST_FUNC(DynamicBitAllocator)
		void TestResizeUp();
	};
}
