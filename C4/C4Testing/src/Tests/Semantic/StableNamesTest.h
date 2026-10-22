#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Names of private data by what the data is (Backend/CodeGen/StableNames.h).
	CLASS(GROUP = Testing) StableNamesTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::StableNamesTest);

		TEST_FUNC(CodeGen.StableNames)
		void TheSameConstantInAnotherContextHasTheSameName();
		TEST_FUNC(CodeGen.StableNames)
		void EachPartOfAConstantChangesItsName();
		TEST_FUNC(CodeGen.StableNames)
		void TheRowTypeChangesTheName();
	};
} // namespace Testing
