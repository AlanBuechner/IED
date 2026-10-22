#pragma once

#include "UnitTesting/TestBase.h"
#include "Core/Hash.h"

namespace Testing
{
	CLASS(GROUP = Testing) HashTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::HashTest);

		TEST_FUNC(Hash)
		void TestConsistentHash();

		TEST_FUNC(Hash)
		void TestDifferentStrings();

		TEST_FUNC(Hash)
		void TestIntegerHash();
	};
}
