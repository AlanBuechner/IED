#include "SingleCharOperatorTest.h"

namespace Testing
{
	// Consolidated: test all delimiter single-char tokens via NextToken (atomic)
	void SingleCharOperatorTest::TestDelimiters()
	{
		auto stream = C4Compiler::Source::FromMemory("{ } [ ] ( ) ; , . : ?");
		C4Compiler::Lexer lexer(*stream, 0);

		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::OpenCurl);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::CloseCurl);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::OpenBracket);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::CloseBracket);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::OpenParen);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::CloseParen);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::SemiColon);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Comma);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Dot);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Colon);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::QuestionMark);
		TEST_PASSED();
	}

	// Consolidated: test all operator single-char tokens via NextToken (atomic)
	// Use NextToken to get atomic tokens; NextCompoundToken would merge % + = into PercentEquals etc.
	void SingleCharOperatorTest::TestAllOperators()
	{
		auto stream = C4Compiler::Source::FromMemory("+ - * / % = & | ^ ~ ! < > @ \\");
		C4Compiler::Lexer lexer(*stream, 0);

		// Arithmetic operators (atomic via NextToken)
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Plus);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Minus);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Star);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::ForwardSlash);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Percent);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Equal);

		// Bitwise and logical operators
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Ampersand);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Pipe);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Caret);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Tilde);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Exclamation);

		// Punctuation and other
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Less);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::Greater);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::At);
		TEST_ASSERT(lexer.NextToken().GetType() == C4Compiler::TokenType::BackSlash);

		TEST_PASSED();
	}
}
