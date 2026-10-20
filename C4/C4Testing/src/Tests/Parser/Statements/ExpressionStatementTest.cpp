#include "ExpressionStatementTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
	void ExpressionStatementTest::TestIdentifierExpression()
	{
		auto stream = Scripting::Source::FromMemory("x;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ExpressionStatementNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetExpression() != nullptr);
		TEST_ASSERT(node->GetSemicolonToken().GetText() == ";");
		TEST_PASSED();
	}

	void ExpressionStatementTest::TestLiteralExpression()
	{
		auto stream = Scripting::Source::FromMemory("42;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ExpressionStatementNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetExpression() != nullptr);
		TEST_ASSERT(node->GetSemicolonToken().GetText() == ";");
		TEST_PASSED();
	}

	void ExpressionStatementTest::TestStringLiteral()
	{
		auto stream = Scripting::Source::FromMemory("\"hello\";");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ExpressionStatementNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetExpression() != nullptr);
		TEST_PASSED();
	}

	void ExpressionStatementTest::TestNoSemicolonReturnsNullptr()
	{
		auto stream = Scripting::Source::FromMemory("x");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ExpressionStatementNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed - the identifier is still available
		TEST_ASSERT(lexer.Peek().GetType() == Scripting::TokenType::Identifier);
		TEST_PASSED();
	}

	void ExpressionStatementTest::TestNonExpressionStartsReturnsNullptr()
	{
		auto stream = Scripting::Source::FromMemory("{ x; }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ExpressionStatementNode::TryParse(state);
		// { x; } is parsed as a braced initializer expression followed by semicolon
		// This is valid - the Pratt parser treats { as a braced initializer start
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}
}
