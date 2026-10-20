#include "SingleCharOperatorTest.h"

namespace Testing
{
	// Consolidated: test all delimiter single-char tokens via NextToken (atomic)
	void SingleCharOperatorTest::TestDelimiters()
	{
		auto stream = Scripting::Source::FromMemory("{ } [ ] ( ) ; , . : ?");
		Scripting::Lexer lexer(*stream, 0);

		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::OpenCurl);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::CloseCurl);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::OpenBracket);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::CloseBracket);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::OpenParen);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::CloseParen);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::SemiColon);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Comma);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Dot);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Colon);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::QuestionMark);
		TEST_PASSED();
	}

	// Consolidated: test all operator single-char tokens via NextToken (atomic)
	// Use NextToken to get atomic tokens; NextCompoundToken would merge % + = into PercentEquals etc.
	void SingleCharOperatorTest::TestAllOperators()
	{
		auto stream = Scripting::Source::FromMemory("+ - * / % = & | ^ ~ ! < > @ \\");
		Scripting::Lexer lexer(*stream, 0);

		// Arithmetic operators (atomic via NextToken)
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Plus);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Minus);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Star);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::ForwardSlash);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Percent);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Equal);

		// Bitwise and logical operators
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Ampersand);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Pipe);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Caret);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Tilde);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Exclamation);

		// Punctuation and other
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Less);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::Greater);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::At);
		TEST_ASSERT(lexer.NextToken().GetType() == Scripting::TokenType::BackSlash);

		TEST_PASSED();
	}
}
