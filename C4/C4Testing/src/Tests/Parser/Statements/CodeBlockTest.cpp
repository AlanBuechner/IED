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

	// A keyword naming a variable is one error, whatever the type looks like, and the block goes on (BUG-306).
	void CodeBlockTest::TestKeywordNamesOneErrorEach()
	{
		auto stream = Scripting::Source::FromMemory(
			"{ Core::String alias = Core::String(\"x\"); Box* asm = null; Core::List<int32> this; int32 a, new; int32 n = 1; }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->StatementCount() == 5);
		const auto& errors = errorTracker.GetErrors();
		TEST_ASSERT(errors.Count() == 4);
		TEST_ASSERT(errors[0].message == "'alias' is a keyword: it cannot name a variable, parameter or function");
		TEST_ASSERT(errors[1].message == "'asm' is a keyword: it cannot name a variable, parameter or function");
		TEST_ASSERT(errors[2].message == "'this' is a keyword: it cannot name a variable, parameter or function");
		TEST_ASSERT(errors[3].message == "'new' is a keyword: it cannot name a variable, parameter or function");
		TEST_PASSED();
	}
}
