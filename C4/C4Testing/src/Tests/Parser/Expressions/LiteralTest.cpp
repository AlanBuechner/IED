#include "LiteralTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
	void LiteralTest::TestIntegerLiteral()
	{
		auto stream = Scripting::Source::FromMemory("42");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::LiteralNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetKind() == Scripting::LiteralNode::LiteralKind::Number);
		TEST_ASSERT(node->GetValue() == "42");
		TEST_PASSED();
	}

	void LiteralTest::TestFloatLiteral()
	{
		auto stream = Scripting::Source::FromMemory("3.14f");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::LiteralNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetKind() == Scripting::LiteralNode::LiteralKind::Number);
		TEST_ASSERT(node->GetValue() == "3.14f");
		TEST_PASSED();
	}

	void LiteralTest::TestStringLiteral()
	{
		auto stream = Scripting::Source::FromMemory("\"hello\"");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::LiteralNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetKind() == Scripting::LiteralNode::LiteralKind::String);
		TEST_PASSED();
	}

	void LiteralTest::TestBooleanTrue()
	{
		auto stream = Scripting::Source::FromMemory("true");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::LiteralNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetKind() == Scripting::LiteralNode::LiteralKind::Boolean);
		TEST_PASSED();
	}

	void LiteralTest::TestBooleanFalse()
	{
		auto stream = Scripting::Source::FromMemory("false");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::LiteralNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetKind() == Scripting::LiteralNode::LiteralKind::Boolean);
		TEST_PASSED();
	}

	void LiteralTest::TestNullLiteral()
	{
		auto stream = Scripting::Source::FromMemory("null");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::LiteralNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetKind() == Scripting::LiteralNode::LiteralKind::Null);
		TEST_PASSED();
	}

	void LiteralTest::TestIdentifierReturnsNullptr()
	{
		auto stream = Scripting::Source::FromMemory("x");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::LiteralNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed - the identifier is still available
		TEST_ASSERT(lexer.Peek().GetType() == Scripting::TokenType::Identifier);
		TEST_PASSED();
	}
}
