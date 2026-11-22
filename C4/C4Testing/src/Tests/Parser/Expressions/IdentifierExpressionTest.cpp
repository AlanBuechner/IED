#include "IdentifierExpressionTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
	void IdentifierExpressionTest::TestSimpleIdentifier()
	{
		auto stream = C4Compiler::Source::FromMemory("x");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::IdentifierExpressionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetNameToken().GetText() == "x");
		TEST_ASSERT(node->GetName() == "x");
		TEST_ASSERT(node->ToString() == "x");
		TEST_PASSED();
	}

	void IdentifierExpressionTest::TestIdentifierAfterOperator()
	{
		auto stream = C4Compiler::Source::FromMemory("+ x");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		// Skip the '+' operator
		lexer.NextToken();

		auto node = C4Compiler::IdentifierExpressionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetNameToken().GetText() == "x");
		TEST_PASSED();
	}

	void IdentifierExpressionTest::TestNumberLiteralReturnsNullptr()
	{
		auto stream = C4Compiler::Source::FromMemory("42");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::IdentifierExpressionNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed - the number is still available
		TEST_ASSERT(lexer.Peek().GetType() == C4Compiler::TokenType::Number);
		TEST_PASSED();
	}

	void IdentifierExpressionTest::TestKeywordClassReturnsNullptr()
	{
		auto stream = C4Compiler::Source::FromMemory("class Foo {}");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		// 'class' is a keyword token type, not an identifier - should not match
		auto node = C4Compiler::IdentifierExpressionNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed - the keyword is still available
		TEST_ASSERT(lexer.Peek().GetType() == C4Compiler::TokenType::Class);
		TEST_PASSED();
	}
}
