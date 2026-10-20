#include "IdentifierExpressionTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
	void IdentifierExpressionTest::TestSimpleIdentifier()
	{
		auto stream = Scripting::Source::FromMemory("x");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::IdentifierExpressionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetNameToken().GetText() == "x");
		TEST_ASSERT(node->GetName() == "x");
		TEST_ASSERT(node->ToString() == "x");
		TEST_PASSED();
	}

	void IdentifierExpressionTest::TestIdentifierAfterOperator()
	{
		auto stream = Scripting::Source::FromMemory("+ x");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		// Skip the '+' operator
		lexer.NextToken();

		auto node = Scripting::IdentifierExpressionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetNameToken().GetText() == "x");
		TEST_PASSED();
	}

	void IdentifierExpressionTest::TestNumberLiteralReturnsNullptr()
	{
		auto stream = Scripting::Source::FromMemory("42");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::IdentifierExpressionNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed - the number is still available
		TEST_ASSERT(lexer.Peek().GetType() == Scripting::TokenType::Number);
		TEST_PASSED();
	}

	void IdentifierExpressionTest::TestKeywordClassReturnsNullptr()
	{
		auto stream = Scripting::Source::FromMemory("class Foo {}");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		// 'class' is a keyword token type, not an identifier - should not match
		auto node = Scripting::IdentifierExpressionNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed - the keyword is still available
		TEST_ASSERT(lexer.Peek().GetType() == Scripting::TokenType::Class);
		TEST_PASSED();
	}
}
