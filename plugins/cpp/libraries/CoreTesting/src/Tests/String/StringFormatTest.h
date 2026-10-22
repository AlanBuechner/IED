#pragma once

#include "UnitTesting/TestBase.h"
#include "Core/StringFormat.h"

namespace Testing
{
	CLASS(GROUP = Testing) StringFormatTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::StringFormatTest);

		TEST_FUNC(StringFormat)
		void TestIntegerFormatting();

		TEST_FUNC(StringFormat)
		void TestFloatFormatting();

		TEST_FUNC(StringFormat)
		void TestStringInsertion();
	};
}
