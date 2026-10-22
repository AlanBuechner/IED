#pragma once

#include "UnitTesting/TestBase.h"
#include "Core/UUID.h"

namespace Testing
{
	CLASS(GROUP = Testing) UUIDTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::UUIDTest);

		TEST_FUNC(UUID)
		void TestUniqueGeneration();

		TEST_FUNC(UUID)
		void TestToStringAndParse();

		TEST_FUNC(UUID)
		void TestEqualityComparison();
	};
}
