#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Lexer/Lexer.h"

namespace Testing
{
	CLASS(GROUP = Testing) LexerFlowTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LexerFlowTest);

		// Peek/NextToken sequencing
		TEST_FUNC(Lexer.Flow)
		void TestPeekReturnsCurrentToken();

		TEST_FUNC(Lexer.Flow)
		void TestNextTokenAdvancesStream();

		TEST_FUNC(Lexer.Flow)
		void TestPeekAheadMultipleTokens();

		// Buffering mechanics
		TEST_FUNC(Lexer.Flow)
		void TestGetNextTokenCombinesPeekAndAdvance();

		TEST_FUNC(Lexer.Flow)
		void TestSequentialSingleCharTokens();

		TEST_FUNC(Lexer.Flow)
		void TestMixedTokensInSequence();

		TEST_FUNC(Lexer.Flow)
		void TestPeekAfterMultipleNextTokenCalls();
	};
}
