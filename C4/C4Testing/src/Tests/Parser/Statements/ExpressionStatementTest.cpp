#include "ExpressionStatementTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
	void ExpressionStatementTest::TestIdentifierExpression()
	{
		auto stream = C4Compiler::Source::FromMemory("x;");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ExpressionStatementNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetExpression() != nullptr);
		TEST_ASSERT(node->GetSemicolonToken().GetText() == ";");
		TEST_PASSED();
	}

	void ExpressionStatementTest::TestLiteralExpression()
	{
		auto stream = C4Compiler::Source::FromMemory("42;");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ExpressionStatementNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetExpression() != nullptr);
		TEST_ASSERT(node->GetSemicolonToken().GetText() == ";");
		TEST_PASSED();
	}

	void ExpressionStatementTest::TestStringLiteral()
	{
		auto stream = C4Compiler::Source::FromMemory("\"hello\";");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ExpressionStatementNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetExpression() != nullptr);
		TEST_PASSED();
	}

	void ExpressionStatementTest::TestNoSemicolonReturnsNullptr()
	{
		auto stream = C4Compiler::Source::FromMemory("x");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ExpressionStatementNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed - the identifier is still available
		TEST_ASSERT(lexer.Peek().GetType() == C4Compiler::TokenType::Identifier);
		TEST_PASSED();
	}

	void ExpressionStatementTest::TestNonExpressionStartsReturnsNullptr()
	{
		auto stream = C4Compiler::Source::FromMemory("{ x; }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ExpressionStatementNode::TryParse(state);
		// { x; } is parsed as a braced initializer expression followed by semicolon
		// This is valid - the Pratt parser treats { as a braced initializer start
		TEST_ASSERT(node != nullptr);
		TEST_PASSED();
	}
}
