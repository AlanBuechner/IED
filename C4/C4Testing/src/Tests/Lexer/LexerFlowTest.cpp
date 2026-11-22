#include "LexerFlowTest.h"
#include "SourceManager/Source.h"

namespace Testing
{
	void LexerFlowTest::TestPeekReturnsCurrentToken()
	{
		// Source: a single identifier followed by whitespace then another token
		auto stream = C4Compiler::Source::FromMemory("hello");
		C4Compiler::Lexer lexer(*stream, 0);

		auto token = lexer.Peek();
		TEST_ASSERT(token.GetType() == C4Compiler::TokenType::Identifier);
		TEST_PASSED();
	}

	void LexerFlowTest::TestNextTokenAdvancesStream()
	{
		auto stream = C4Compiler::Source::FromMemory("hello world");
		C4Compiler::Lexer lexer(*stream, 0);

		auto first = lexer.Peek();
		TEST_ASSERT(first.GetType() == C4Compiler::TokenType::Identifier);

		lexer.NextToken();

		auto second = lexer.Peek();
		TEST_ASSERT(second.GetType() == C4Compiler::TokenType::Identifier);
		TEST_PASSED();
	}

	void LexerFlowTest::TestPeekAheadMultipleTokens()
	{
		auto stream = C4Compiler::Source::FromMemory("+ - *");
		C4Compiler::Lexer lexer(*stream, 0);

		auto t0 = lexer.Peek(0);
		TEST_ASSERT(t0.GetType() == C4Compiler::TokenType::Plus);

		auto t1 = lexer.Peek(1);
		TEST_ASSERT(t1.GetType() == C4Compiler::TokenType::Minus);

		auto t2 = lexer.Peek(2);
		TEST_ASSERT(t2.GetType() == C4Compiler::TokenType::Star);
		TEST_PASSED();
	}

	void LexerFlowTest::TestGetNextTokenCombinesPeekAndAdvance()
	{
		auto stream = C4Compiler::Source::FromMemory("{ }");
		C4Compiler::Lexer lexer(*stream, 0);

		auto open = lexer.NextCompoundToken();
		TEST_ASSERT(open.GetType() == C4Compiler::TokenType::OpenCurl);

		auto close = lexer.NextCompoundToken();
		TEST_ASSERT(close.GetType() == C4Compiler::TokenType::CloseCurl);
		TEST_PASSED();
	}

	void LexerFlowTest::TestSequentialSingleCharTokens()
	{
		auto stream = C4Compiler::Source::FromMemory("+ - * /");
		C4Compiler::Lexer lexer(*stream, 0);

		auto t0 = lexer.NextCompoundToken();
		TEST_ASSERT(t0.GetType() == C4Compiler::TokenType::Plus);

		auto t1 = lexer.NextCompoundToken();
		TEST_ASSERT(t1.GetType() == C4Compiler::TokenType::Minus);

		auto t2 = lexer.NextCompoundToken();
		TEST_ASSERT(t2.GetType() == C4Compiler::TokenType::Star);

		auto t3 = lexer.NextCompoundToken();
		TEST_ASSERT(t3.GetType() == C4Compiler::TokenType::ForwardSlash);
		TEST_PASSED();
	}

	void LexerFlowTest::TestMixedTokensInSequence()
	{
		auto stream = C4Compiler::Source::FromMemory("x + 1");
		C4Compiler::Lexer lexer(*stream, 0);

		auto ident = lexer.NextCompoundToken();
		TEST_ASSERT(ident.GetType() == C4Compiler::TokenType::Identifier);

		auto plus = lexer.NextCompoundToken();
		TEST_ASSERT(plus.GetType() == C4Compiler::TokenType::Plus);

		auto num = lexer.NextCompoundToken();
		TEST_ASSERT(num.GetType() == C4Compiler::TokenType::Number);
		TEST_PASSED();
	}

	void LexerFlowTest::TestPeekAfterMultipleNextTokenCalls()
	{
		auto stream = C4Compiler::Source::FromMemory("a b c");
		C4Compiler::Lexer lexer(*stream, 0);

		// Advance past first two tokens using GetNextToken (safe: Peek + NextToken)
		lexer.NextCompoundToken(); // skip 'a'
		lexer.NextCompoundToken(); // skip 'b'

		auto current = lexer.Peek();
		TEST_ASSERT(current.GetType() == C4Compiler::TokenType::Identifier);
		TEST_PASSED();
	}
}
