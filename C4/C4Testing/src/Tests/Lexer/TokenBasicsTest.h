#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Lexer/Token.h"

namespace Testing
{
	CLASS(GROUP = Testing) TokenBasicsTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::TokenBasicsTest);

		TEST_FUNC(Token.Basics)
		void TestDefaultConstruction();

		TEST_FUNC(Token.Basics)
		void TestTypeConstructor();

		TEST_FUNC(Token.Basics)
		void TestSourceLocationDefaults();

		TEST_FUNC(Token.Basics)
		void TestSetStartEndLocation();

		TEST_FUNC(Token.Basics)
		void TestEmptyTokenText();
	};
}
