#include "CodeBlockTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"

namespace Testing
{
	void CodeBlockTest::TestEmptyBlock()
	{
		auto stream = C4Compiler::Source::FromMemory("{}");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->HasBraces());
		TEST_ASSERT(node->StatementCount() == 0);
		TEST_PASSED();
	}

	void CodeBlockTest::TestSingleExpressionStatement()
	{
		auto stream = C4Compiler::Source::FromMemory("{ x; }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->HasBraces());
		TEST_ASSERT(node->StatementCount() == 1);
		TEST_PASSED();
	}

	void CodeBlockTest::TestSingleLiteralStatement()
	{
		auto stream = C4Compiler::Source::FromMemory("{ 42; }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->HasBraces());
		TEST_ASSERT(node->StatementCount() == 1);
		TEST_PASSED();
	}

	void CodeBlockTest::TestMultipleStatements()
	{
		auto stream = C4Compiler::Source::FromMemory("{ a(); b(); c(); }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->HasBraces());
		TEST_ASSERT(node->StatementCount() == 3);
		TEST_PASSED();
	}

	void CodeBlockTest::TestNestedBlock()
	{
		auto stream = C4Compiler::Source::FromMemory("{ { inner(); } outer(); }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->HasBraces());
		TEST_ASSERT(node->StatementCount() == 2);
		TEST_PASSED();
	}

	void CodeBlockTest::TestNoOpenBraceReturnsNullptr()
	{
		auto stream = C4Compiler::Source::FromMemory("x = 5;");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed - the identifier is still available
		TEST_ASSERT(lexer.Peek().GetType() == C4Compiler::TokenType::Identifier);
		TEST_PASSED();
	}

	// A keyword naming a variable is one error, whatever the type looks like, and the block goes on (BUG-306).
	void CodeBlockTest::TestKeywordNamesOneErrorEach()
	{
		auto stream = C4Compiler::Source::FromMemory(
			"{ Core::String alias = Core::String(\"x\"); Box* asm = null; Core::List<int32> this; int32 a, new; int32 n = 1; }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::CodeBlockNode::TryParse(state);
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

	// A statement no parser takes is one error, and the block goes on after it (BUG-311): it once reported again at the
	// token it stopped on, then at nearly every token after it.
	void CodeBlockTest::TestUnparsedStatementIsOneError()
	{
		auto stream = C4Compiler::Source::FromMemory(
			"{ int32 x = 1; return alias + 1; int32 z = Apply([&alias](int32 v) -> int32 { return v + x; }); ] ) garbage ( ; int32 y = 2; }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::CodeBlockNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		const auto& errors = errorTracker.GetErrors();
		TEST_ASSERT(errors.Count() == 3);
		TEST_ASSERT(errors[0].message == "'alias' is a keyword: it cannot name a variable, parameter or function");
		TEST_ASSERT(errors[1].message == "Expected a capture in lambda, got 'alias'");
		TEST_ASSERT(errors[2].message == "Expected expression, got ']'");
		const auto* last = dynamic_cast<const C4Compiler::VariableDefinitionNode*>(node->GetStatements()[node->StatementCount() - 1].Get());
		TEST_ASSERT(last != nullptr && last->GetNameToken().GetText() == "y"); // the block went on after the bad statements
		TEST_PASSED();
	}
}
