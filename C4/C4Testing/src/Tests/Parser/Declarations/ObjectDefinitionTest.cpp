#include "ObjectDefinitionTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"

namespace Testing
{
	void ObjectDefinitionTest::TestSimpleClass()
	{
		auto stream = Scripting::Source::FromMemory("class MyClass { }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->Kind() == Scripting::ObjectKind::Class);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestStruct()
	{
		auto stream = Scripting::Source::FromMemory("struct MyStruct { }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->Kind() == Scripting::ObjectKind::Struct);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestInterface()
	{
		auto stream = Scripting::Source::FromMemory("interface IMyInterface { }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->Kind() == Scripting::ObjectKind::Interface);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestWithAccessModifier()
	{
		auto stream = Scripting::Source::FromMemory("public class MyClass { }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetAccessModifier() == Scripting::AccessModifier::Public);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestWithInheritance()
	{
		auto stream = Scripting::Source::FromMemory("class MyClass : BaseClass { }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetBaseClasses().Count() == 1);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestMultipleInheritance()
	{
		auto stream = Scripting::Source::FromMemory("class MyClass : BaseClass1, BaseClass2 { }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetBaseClasses().Count() == 2);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestWithTemplate()
	{
		auto stream = Scripting::Source::FromMemory("template<T> class MyClass { }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsTemplated());
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestWithAnnotation()
	{
		auto stream = Scripting::Source::FromMemory("[Serialize] class MyClass { }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetAnnotations().Count() == 1);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestAbstractClass()
	{
		auto stream = Scripting::Source::FromMemory("abstract class MyClass { }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsAbstract());
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestFinalClass()
	{
		auto stream = Scripting::Source::FromMemory("final class Leaf : public Base { }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsFinal());
		TEST_ASSERT(!node->IsAbstract());
		TEST_ASSERT(node->GetNameToken().GetText() == "Leaf");
		TEST_ASSERT(errorTracker.GetErrors().Count() == 0);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestClassModifierConflicts()
	{
		const char* sources[] = { "abstract final class Shape { }", "abstract interface IShape { }", "final interface IShape { }",
			"final final class Leaf { }" };
		for (const char* source : sources)
		{
			auto stream = Scripting::Source::FromMemory(source);
			Scripting::Lexer lexer(*stream, 0);
			Scripting::ErrorTracker errorTracker;
			Scripting::ParserState state(lexer, errorTracker);

			auto node = Scripting::ObjectDefinitionNode::TryParse(state);
			TEST_ASSERT(node != nullptr);
			TEST_ASSERT(errorTracker.GetErrors().Count() == 1);
		}
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestNoMatch()
	{
		auto stream = Scripting::Source::FromMemory("while (x > 0) { }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		TEST_PASSED();
	}
}
