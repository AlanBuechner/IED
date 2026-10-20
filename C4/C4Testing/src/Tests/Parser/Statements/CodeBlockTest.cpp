#include "CodeBlockTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
	void CodeBlockTest::TestEmptyBlock()
	{
		auto stream = Scripting::Source::FromMemory("{}");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->HasBraces());
		TEST_ASSERT(node->StatementCount() == 0);
		TEST_PASSED();
	}

	void CodeBlockTest::TestSingleExpressionStatement()
	{
		auto stream = Scripting::Source::FromMemory("{ x; }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->HasBraces());
		TEST_ASSERT(node->StatementCount() == 1);
		TEST_PASSED();
	}

	void CodeBlockTest::TestSingleLiteralStatement()
	{
		auto stream = Scripting::Source::FromMemory("{ 42; }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->HasBraces());
		TEST_ASSERT(node->StatementCount() == 1);
		TEST_PASSED();
	}

	void CodeBlockTest::TestMultipleStatements()
	{
		auto stream = Scripting::Source::FromMemory("{ a(); b(); c(); }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->HasBraces());
		TEST_ASSERT(node->StatementCount() == 3);
		TEST_PASSED();
	}

	void CodeBlockTest::TestNestedBlock()
	{
		auto stream = Scripting::Source::FromMemory("{ { inner(); } outer(); }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->HasBraces());
		TEST_ASSERT(node->StatementCount() == 2);
		TEST_PASSED();
	}

	void CodeBlockTest::TestNoOpenBraceReturnsNullptr()
	{
		auto stream = Scripting::Source::FromMemory("x = 5;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed - the identifier is still available
		TEST_ASSERT(lexer.Peek().GetType() == Scripting::TokenType::Identifier);
		TEST_PASSED();
	}
}
