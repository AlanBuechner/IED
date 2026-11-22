#include "LiteralTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
	void LiteralTest::TestIntegerLiteral()
	{
		auto stream = C4Compiler::Source::FromMemory("42");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::LiteralNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetKind() == C4Compiler::LiteralNode::LiteralKind::Number);
		TEST_ASSERT(node->GetValue() == "42");
		TEST_PASSED();
	}

	void LiteralTest::TestFloatLiteral()
	{
		auto stream = C4Compiler::Source::FromMemory("3.14f");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::LiteralNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetKind() == C4Compiler::LiteralNode::LiteralKind::Number);
		TEST_ASSERT(node->GetValue() == "3.14f");
		TEST_PASSED();
	}

	void LiteralTest::TestStringLiteral()
	{
		auto stream = C4Compiler::Source::FromMemory("\"hello\"");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::LiteralNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetKind() == C4Compiler::LiteralNode::LiteralKind::String);
		TEST_PASSED();
	}

	void LiteralTest::TestBooleanTrue()
	{
		auto stream = C4Compiler::Source::FromMemory("true");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::LiteralNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetKind() == C4Compiler::LiteralNode::LiteralKind::Boolean);
		TEST_PASSED();
	}

	void LiteralTest::TestBooleanFalse()
	{
		auto stream = C4Compiler::Source::FromMemory("false");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::LiteralNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetKind() == C4Compiler::LiteralNode::LiteralKind::Boolean);
		TEST_PASSED();
	}

	void LiteralTest::TestNullLiteral()
	{
		auto stream = C4Compiler::Source::FromMemory("null");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::LiteralNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetKind() == C4Compiler::LiteralNode::LiteralKind::Null);
		TEST_PASSED();
	}

	void LiteralTest::TestIdentifierReturnsNullptr()
	{
		auto stream = C4Compiler::Source::FromMemory("x");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::LiteralNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed - the identifier is still available
		TEST_ASSERT(lexer.Peek().GetType() == C4Compiler::TokenType::Identifier);
		TEST_PASSED();
	}
}
