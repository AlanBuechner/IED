#include "VariableDefinitionTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
	void VariableDefinitionTest::TestSimpleVariable()
	{
		auto stream = C4Compiler::Source::FromMemory("int x = 0;");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetAccessModifier() == C4Compiler::AccessModifier::None);
		TEST_ASSERT(!node->IsStatic());
		TEST_ASSERT(!node->IsConst());
		TEST_ASSERT(node->GetNameToken().GetText() == "x");
		TEST_ASSERT(node->HasInitializer());
		TEST_PASSED();
	}

	void VariableDefinitionTest::TestWithAccessModifier()
	{
		auto stream = C4Compiler::Source::FromMemory("public int x;");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetAccessModifier() == C4Compiler::AccessModifier::Public);
		TEST_PASSED();
	}

	void VariableDefinitionTest::TestStatic()
	{
		auto stream = C4Compiler::Source::FromMemory("static float speed = 5.0f;");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsStatic());
		TEST_PASSED();
	}

	void VariableDefinitionTest::TestConst()
	{
		auto stream = C4Compiler::Source::FromMemory("const int MAX = 100;");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsConst());
		TEST_PASSED();
	}

	void VariableDefinitionTest::TestComplexType()
	{
		auto stream = C4Compiler::Source::FromMemory("const NC::Vector<int>* ptr = nullptr;");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetType() != nullptr);
		TEST_PASSED();
	}

	void VariableDefinitionTest::TestWithAnnotation()
	{
		auto stream = C4Compiler::Source::FromMemory("[Serialize] int value;");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetAnnotations().Count() == 1);
		TEST_PASSED();
	}

	void VariableDefinitionTest::TestNoMatch()
	{
		auto stream = C4Compiler::Source::FromMemory("while (x > 0) { }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		TEST_PASSED();
	}
}
