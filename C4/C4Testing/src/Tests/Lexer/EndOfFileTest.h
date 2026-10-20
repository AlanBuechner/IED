#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Lexer/Lexer.h"

namespace Testing
{
	CLASS(GROUP = Testing) EndOfFileTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::EndOfFileTest);

		// EOF token generation
		TEST_FUNC(Lexer.EndOfFile)
		void TestEOFTokenAfterEmptyStream();

		TEST_FUNC(Lexer.EndOfFile)
		void TestEOFTokenAfterSingleToken();

		TEST_FUNC(Lexer.EndOfFile)
		void TestEOFTokenTypeIsEndOfFile();

		// EOF location tracking
		TEST_FUNC(Lexer.EndOfFile)
		void TestEofLocationAfterContent();

		TEST_FUNC(Lexer.EndOfFile)
		void TestEofStartEqualsEndLocation();

		// EOF behavior with GetNextToken
		TEST_FUNC(Lexer.EndOfFile)
		void TestGetNextTokenReturnsEOFAtEnd();

		TEST_FUNC(Lexer.EndOfFile)
		void TestMultiplePeekAfterEOF();
	};
}
