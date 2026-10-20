#include "VariableDefinitionTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
	void VariableDefinitionTest::TestSimpleVariable()
	{
		auto stream = Scripting::Source::FromMemory("int x = 0;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetAccessModifier() == Scripting::AccessModifier::None);
		TEST_ASSERT(!node->IsStatic());
		TEST_ASSERT(!node->IsConst());
		TEST_ASSERT(node->GetNameToken().GetText() == "x");
		TEST_ASSERT(node->HasInitializer());
		TEST_PASSED();
	}

	void VariableDefinitionTest::TestWithAccessModifier()
	{
		auto stream = Scripting::Source::FromMemory("public int x;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetAccessModifier() == Scripting::AccessModifier::Public);
		TEST_PASSED();
	}

	void VariableDefinitionTest::TestStatic()
	{
		auto stream = Scripting::Source::FromMemory("static float speed = 5.0f;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsStatic());
		TEST_PASSED();
	}

	void VariableDefinitionTest::TestConst()
	{
		auto stream = Scripting::Source::FromMemory("const int MAX = 100;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsConst());
		TEST_PASSED();
	}

	void VariableDefinitionTest::TestComplexType()
	{
		auto stream = Scripting::Source::FromMemory("const NC::Vector<int>* ptr = nullptr;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetType() != nullptr);
		TEST_PASSED();
	}

	void VariableDefinitionTest::TestWithAnnotation()
	{
		auto stream = Scripting::Source::FromMemory("[Serialize] int value;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetAnnotations().Count() == 1);
		TEST_PASSED();
	}

	void VariableDefinitionTest::TestNoMatch()
	{
		auto stream = Scripting::Source::FromMemory("while (x > 0) { }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::VariableDefinitionNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		TEST_PASSED();
	}
}
