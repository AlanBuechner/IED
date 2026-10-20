#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Lexer/Lexer.h"
#include "SourceManager/Source.h"

namespace Testing
{
	CLASS(GROUP = Testing) SingleCharOperatorTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::SingleCharOperatorTest);

		TEST_FUNC(Lexer.SingleCharOperators.Delimiters)
		void TestDelimiters();

		// Consolidated: arithmetic, bitwise/logical, and punctuation operators in one pass
		TEST_FUNC(Lexer.SingleCharOperators.AllOperators)
		void TestAllOperators();
	};
}
