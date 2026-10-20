#include "TemplateDefinitionTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
	void TemplateDefinitionTest::TestSingleTypeParam()
	{
		auto stream = Scripting::Source::FromMemory("template<T>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::TemplateDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 1);
		{
			auto param = node->GetParameter(0);
			TEST_ASSERT(param->Kind() == Scripting::TemplateParameterKind::Type);
			TEST_ASSERT(param->GetName() == "T");
		}
		TEST_ASSERT(node->ToString() == "template<T>");
		TEST_PASSED();
	}

	void TemplateDefinitionTest::TestMultipleParams()
	{
		auto stream = Scripting::Source::FromMemory("template<T, U>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::TemplateDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 2);
		{
			auto param0 = node->GetParameter(0);
			auto param1 = node->GetParameter(1);
			TEST_ASSERT(param0->GetName() == "T");
			TEST_ASSERT(param1->GetName() == "U");
		}
		TEST_ASSERT(node->ToString() == "template<T, U>");
		TEST_PASSED();
	}

	void TemplateDefinitionTest::TestNonTypeParam()
	{
		auto stream = Scripting::Source::FromMemory("template<uint32 I>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::TemplateDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 1);
		{
			auto param = node->GetParameter(0);
			TEST_ASSERT(param->Kind() == Scripting::TemplateParameterKind::NonType);
			TEST_ASSERT(param->GetName() == "I");
		}
		TEST_ASSERT(node->ToString() == "template<uint32 I>");
		TEST_PASSED();
	}

	void TemplateDefinitionTest::TestMixedParams()
	{
		auto stream = Scripting::Source::FromMemory("template<T, uint32 I>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::TemplateDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 2);
		{
			auto param0 = node->GetParameter(0);
			auto param1 = node->GetParameter(1);
			TEST_ASSERT(param0->Kind() == Scripting::TemplateParameterKind::Type);
			TEST_ASSERT(param0->GetName() == "T");
			TEST_ASSERT(param1->Kind() == Scripting::TemplateParameterKind::NonType);
			TEST_ASSERT(param1->GetName() == "I");
		}
		TEST_ASSERT(node->ToString() == "template<T, uint32 I>");
		TEST_PASSED();
	}

	void TemplateDefinitionTest::TestTypenameKeyword()
	{
		auto stream = Scripting::Source::FromMemory("template<typename T>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::TemplateDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 1);
		{
			auto param = node->GetParameter(0);
			TEST_ASSERT(param->Kind() == Scripting::TemplateParameterKind::Type);
			TEST_ASSERT(param->GetName() == "T");
		}
		TEST_ASSERT(node->ToString() == "template<typename T>");
		TEST_PASSED();
	}

	void TemplateDefinitionTest::TestClassKeyword()
	{
		auto stream = Scripting::Source::FromMemory("template<class T>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::TemplateDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 1);
		{
			auto param = node->GetParameter(0);
			TEST_ASSERT(param->Kind() == Scripting::TemplateParameterKind::Type);
			TEST_ASSERT(param->GetName() == "T");
		}
		TEST_ASSERT(node->ToString() == "template<class T>");
		TEST_PASSED();
	}

	void TemplateDefinitionTest::TestVariadicType()
	{
		auto stream = Scripting::Source::FromMemory("template<...T>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::TemplateDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 1);
		{
			auto param = node->GetParameter(0);
			TEST_ASSERT(param->Kind() == Scripting::TemplateParameterKind::VariadicType);
			TEST_ASSERT(param->GetName() == "T");
		}
		TEST_ASSERT(node->ToString() == "template<...typename T>");
		TEST_PASSED();
	}

	void TemplateDefinitionTest::TestVariadicNonType()
	{
		auto stream = Scripting::Source::FromMemory("template<...uint32 I>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::TemplateDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 1);
		{
			auto param = node->GetParameter(0);
			TEST_ASSERT(param->Kind() == Scripting::TemplateParameterKind::VariadicNonType);
			TEST_ASSERT(param->GetName() == "I");
		}
		TEST_ASSERT(node->ToString() == "template<...uint32 I>");
		TEST_PASSED();
	}

	void TemplateDefinitionTest::TestNoTemplateAtStart()
	{
		auto stream = Scripting::Source::FromMemory("class Foo");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::TemplateDefinitionNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed
		TEST_ASSERT(lexer.Peek().GetText() == "class");
		TEST_PASSED();
	}

	void TemplateDefinitionTest::TestMissingCloseAngle()
	{
		auto stream = Scripting::Source::FromMemory("template<T;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::TemplateDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->ParameterCount() == 1);
		{
			auto param = node->GetParameter(0);
			TEST_ASSERT(param->GetName() == "T");
		}
		// Error should be recorded for missing '>'
		TEST_ASSERT(errorTracker.GetErrors().Count() >= 1);
		TEST_PASSED();
	}
}
