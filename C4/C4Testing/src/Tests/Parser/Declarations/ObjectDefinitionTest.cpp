#include "ObjectDefinitionTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"

namespace Testing
{
	void ObjectDefinitionTest::TestSimpleClass()
	{
		auto stream = C4Compiler::Source::FromMemory("class MyClass { }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->Kind() == C4Compiler::ObjectKind::Class);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestStruct()
	{
		auto stream = C4Compiler::Source::FromMemory("struct MyStruct { }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->Kind() == C4Compiler::ObjectKind::Struct);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestInterface()
	{
		auto stream = C4Compiler::Source::FromMemory("interface IMyInterface { }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->Kind() == C4Compiler::ObjectKind::Interface);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestWithAccessModifier()
	{
		auto stream = C4Compiler::Source::FromMemory("public class MyClass { }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetAccessModifier() == C4Compiler::AccessModifier::Public);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestWithInheritance()
	{
		auto stream = C4Compiler::Source::FromMemory("class MyClass : BaseClass { }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetBaseClasses().Count() == 1);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestMultipleInheritance()
	{
		auto stream = C4Compiler::Source::FromMemory("class MyClass : BaseClass1, BaseClass2 { }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetBaseClasses().Count() == 2);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestWithTemplate()
	{
		auto stream = C4Compiler::Source::FromMemory("template<T> class MyClass { }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsTemplated());
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestWithAnnotation()
	{
		auto stream = C4Compiler::Source::FromMemory("[Serialize] class MyClass { }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetAnnotations().Count() == 1);
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestAbstractClass()
	{
		auto stream = C4Compiler::Source::FromMemory("abstract class MyClass { }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsAbstract());
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestFinalClass()
	{
		auto stream = C4Compiler::Source::FromMemory("final class Leaf : public Base { }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
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
			auto stream = C4Compiler::Source::FromMemory(source);
			C4Compiler::Lexer lexer(*stream, 0);
			C4Compiler::ErrorTracker errorTracker;
			C4Compiler::ParserState state(lexer, errorTracker);

			auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
			TEST_ASSERT(node != nullptr);
			TEST_ASSERT(errorTracker.GetErrors().Count() == 1);
		}
		TEST_PASSED();
	}

	void ObjectDefinitionTest::TestNoMatch()
	{
		auto stream = C4Compiler::Source::FromMemory("while (x > 0) { }");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		TEST_PASSED();
	}

	namespace
	{
		const C4Compiler::VariableDefinitionNode* FieldNamed(const C4Compiler::ObjectDefinitionNode& node, const char* name)
		{
			for (const auto& member : node.GetBodyMembers())
				if (const auto* field = dynamic_cast<const C4Compiler::VariableDefinitionNode*>(member.Get()); field && field->GetNameToken().GetText() == name)
					return field;
			return nullptr;
		}

		Core::String FirstAnnotationName(const Core::Vector<Ref<C4Compiler::AnnotationNode>>& annotations)
		{
			return annotations.Empty() ? Core::String() : annotations[0]->GetBracketGroups()[0].annotations[0]->GetName();
		}
	}

	// `[Property] private:` - each label keeps its annotations, and the fields and methods under it take them.
	void ObjectDefinitionTest::TestSectionAnnotations()
	{
		auto stream = C4Compiler::Source::FromMemory(
			"class Panel {\n"
			"	private int32 before;\n"
			"[Property] [Saved, Shown]\n"
			"private:\n"
			"	private int32 a;\n"
			"	[Own] private void Helper();\n"
			"public:\n"
			"	public int32 plain;\n"
			"[Serialize(1)] protected:\n"
			"	protected int32 kept;\n"
			"}");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT_EQ((int)errorTracker.GetErrors().Count(), 0);
		TEST_ASSERT_EQ((int)node->GetSections().Count(), 3);
		const C4Compiler::AccessSection& hidden = node->GetSections()[0];
		TEST_ASSERT(hidden.m_Access == C4Compiler::AccessModifier::Private);
		TEST_ASSERT_EQ((int)hidden.m_Annotations.Count(), 2); // one node per bracket run: [Property] and [Saved, Shown]
		TEST_ASSERT_EQ((int)hidden.m_Members.Count(), 2);
		TEST_ASSERT(FieldNamed(*node, "before")->GetSectionAnnotations().Empty()); // before any label
		TEST_ASSERT(FirstAnnotationName(FieldNamed(*node, "a")->GetSectionAnnotations()) == "Property");
		TEST_ASSERT(FieldNamed(*node, "a")->GetAnnotations().Empty());           // its own stay its own
		TEST_ASSERT(FieldNamed(*node, "plain")->GetSectionAnnotations().Empty()); // a label without annotations
		TEST_ASSERT(FirstAnnotationName(FieldNamed(*node, "kept")->GetSectionAnnotations()) == "Serialize");
		TEST_ASSERT(FieldNamed(*node, "kept")->GetAccessModifier() == C4Compiler::AccessModifier::Protected);
		for (const auto& member : node->GetBodyMembers())
			if (const auto* method = dynamic_cast<const C4Compiler::FunctionDefinitionNode*>(member.Get()))
				TEST_ASSERT(FirstAnnotationName(method->GetSectionAnnotations()) == "Property" && FirstAnnotationName(method->GetAnnotations()) == "Own");
		TEST_PASSED();
	}

	// Nested types, enums and aliases are no section's members: they take none of its annotations.
	void ObjectDefinitionTest::TestSectionAnnotationsOnlyOnFieldsAndMethods()
	{
		auto stream = C4Compiler::Source::FromMemory(
			"class Holder {\n"
			"[Saved] public:\n"
			"	public enum Mode { A, B }\n"
			"	struct Inner { public int32 x; }\n"
			"	public int32 value;\n"
			"}");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT_EQ((int)errorTracker.GetErrors().Count(), 0);
		TEST_ASSERT_EQ((int)node->GetSections()[0].m_Members.Count(), 1);
		TEST_ASSERT(node->GetSections()[0].m_Members[0] == FieldNamed(*node, "value"));
		TEST_PASSED();
	}

	// The compiler's annotations act on one declaration: a label passes on only reflected ones.
	void ObjectDefinitionTest::TestCompilerAnnotationOnSection()
	{
		auto stream = C4Compiler::Source::FromMemory("class Grouped {\n[group(\"ui\")]\npublic:\n	public int32 value;\n}");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT_EQ((int)errorTracker.GetErrors().Count(), 1);
		TEST_ASSERT(errorTracker.GetErrors()[0].message.find("[group] is the compiler's") != Core::String::npos);
		TEST_PASSED();
	}

	// An annotated member is no label: `[Range(0, 1)] public float x;` stays the field's own.
	void ObjectDefinitionTest::TestAnnotatedMemberIsNoLabel()
	{
		auto stream = C4Compiler::Source::FromMemory("class Plain {\npublic:\n	[Range(0, 1)] public float x;\n	[Command(\"go\")] public void Go() { }\n}");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ObjectDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT_EQ((int)errorTracker.GetErrors().Count(), 0);
		TEST_ASSERT_EQ((int)node->GetSections().Count(), 1);
		TEST_ASSERT(node->GetSections()[0].m_Annotations.Empty());
		TEST_ASSERT(FirstAnnotationName(FieldNamed(*node, "x")->GetAnnotations()) == "Range");
		TEST_PASSED();
	}
}
