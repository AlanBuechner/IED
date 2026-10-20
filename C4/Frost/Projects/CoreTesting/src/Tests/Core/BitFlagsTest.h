#pragma once

#include "UnitTesting/TestBase.h"
#include "Core/BitFlags.h"

namespace Testing
{
	CLASS(GROUP = Testing) BitFlagsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::BitFlagsTest);

		TEST_FUNC(BitFlags)
		void TestSetSingleFlag();

		TEST_FUNC(BitFlags)
		void TestClearFlag();

		TEST_FUNC(BitFlags)
		void TestCheckFlag();
	};
}
