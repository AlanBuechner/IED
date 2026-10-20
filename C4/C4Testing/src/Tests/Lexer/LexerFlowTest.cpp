#include "LexerFlowTest.h"
#include "SourceManager/Source.h"

namespace Testing
{
	void LexerFlowTest::TestPeekReturnsCurrentToken()
	{
		// Source: a single identifier followed by whitespace then another token
		auto stream = Scripting::Source::FromMemory("hello");
		Scripting::Lexer lexer(*stream, 0);

		auto token = lexer.Peek();
		TEST_ASSERT(token.GetType() == Scripting::TokenType::Identifier);
		TEST_PASSED();
	}

	void LexerFlowTest::TestNextTokenAdvancesStream()
	{
		auto stream = Scripting::Source::FromMemory("hello world");
		Scripting::Lexer lexer(*stream, 0);

		auto first = lexer.Peek();
		TEST_ASSERT(first.GetType() == Scripting::TokenType::Identifier);

		lexer.NextToken();

		auto second = lexer.Peek();
		TEST_ASSERT(second.GetType() == Scripting::TokenType::Identifier);
		TEST_PASSED();
	}

	void LexerFlowTest::TestPeekAheadMultipleTokens()
	{
		auto stream = Scripting::Source::FromMemory("+ - *");
		Scripting::Lexer lexer(*stream, 0);

		auto t0 = lexer.Peek(0);
		TEST_ASSERT(t0.GetType() == Scripting::TokenType::Plus);

		auto t1 = lexer.Peek(1);
		TEST_ASSERT(t1.GetType() == Scripting::TokenType::Minus);

		auto t2 = lexer.Peek(2);
		TEST_ASSERT(t2.GetType() == Scripting::TokenType::Star);
		TEST_PASSED();
	}

	void LexerFlowTest::TestGetNextTokenCombinesPeekAndAdvance()
	{
		auto stream = Scripting::Source::FromMemory("{ }");
		Scripting::Lexer lexer(*stream, 0);

		auto open = lexer.NextCompoundToken();
		TEST_ASSERT(open.GetType() == Scripting::TokenType::OpenCurl);

		auto close = lexer.NextCompoundToken();
		TEST_ASSERT(close.GetType() == Scripting::TokenType::CloseCurl);
		TEST_PASSED();
	}

	void LexerFlowTest::TestSequentialSingleCharTokens()
	{
		auto stream = Scripting::Source::FromMemory("+ - * /");
		Scripting::Lexer lexer(*stream, 0);

		auto t0 = lexer.NextCompoundToken();
		TEST_ASSERT(t0.GetType() == Scripting::TokenType::Plus);

		auto t1 = lexer.NextCompoundToken();
		TEST_ASSERT(t1.GetType() == Scripting::TokenType::Minus);

		auto t2 = lexer.NextCompoundToken();
		TEST_ASSERT(t2.GetType() == Scripting::TokenType::Star);

		auto t3 = lexer.NextCompoundToken();
		TEST_ASSERT(t3.GetType() == Scripting::TokenType::ForwardSlash);
		TEST_PASSED();
	}

	void LexerFlowTest::TestMixedTokensInSequence()
	{
		auto stream = Scripting::Source::FromMemory("x + 1");
		Scripting::Lexer lexer(*stream, 0);

		auto ident = lexer.NextCompoundToken();
		TEST_ASSERT(ident.GetType() == Scripting::TokenType::Identifier);

		auto plus = lexer.NextCompoundToken();
		TEST_ASSERT(plus.GetType() == Scripting::TokenType::Plus);

		auto num = lexer.NextCompoundToken();
		TEST_ASSERT(num.GetType() == Scripting::TokenType::Number);
		TEST_PASSED();
	}

	void LexerFlowTest::TestPeekAfterMultipleNextTokenCalls()
	{
		auto stream = Scripting::Source::FromMemory("a b c");
		Scripting::Lexer lexer(*stream, 0);

		// Advance past first two tokens using GetNextToken (safe: Peek + NextToken)
		lexer.NextCompoundToken(); // skip 'a'
		lexer.NextCompoundToken(); // skip 'b'

		auto current = lexer.Peek();
		TEST_ASSERT(current.GetType() == Scripting::TokenType::Identifier);
		TEST_PASSED();
	}
}
