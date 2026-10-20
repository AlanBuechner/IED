#include "AnnotationTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"

namespace Testing
{
	void AnnotationTest::TestSimpleAnnotation()
	{
		auto stream = Scripting::Source::FromMemory("[Serialize]");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::AnnotationNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetBracketGroups().Count() == 1);
		TEST_ASSERT(node->TotalAnnotationCount() == 1);

		const auto& group = node->GetBracketGroups()[0];
		auto item = group.annotations[0];
		TEST_ASSERT(item->GetName() == "Serialize");
		TEST_ASSERT(item->GetArguments().Empty());
		TEST_PASSED();
	}

	void AnnotationTest::TestAnnotationWithStringArg()
	{
		auto stream = Scripting::Source::FromMemory("[Test(\"stuff\")]");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::AnnotationNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetBracketGroups().Count() == 1);
		TEST_ASSERT(node->TotalAnnotationCount() == 1);

		const auto& group = node->GetBracketGroups()[0];
		auto item = group.annotations[0];
		TEST_ASSERT(item->GetName() == "Test");
		TEST_ASSERT(item->GetArguments().Count() == 1);

		auto arg = item->GetArguments()[0];
		TEST_ASSERT(arg->Type() == Scripting::NodeType::LiteralExpression);
		TEST_PASSED();
	}

	void AnnotationTest::TestMultipleAnnotationsInOneBracket()
	{
		auto stream = Scripting::Source::FromMemory("[A, B(1), C]");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::AnnotationNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetBracketGroups().Count() == 1);
		TEST_ASSERT(node->TotalAnnotationCount() == 3);

		const auto& group = node->GetBracketGroups()[0];
		TEST_ASSERT(group.annotations[0]->GetName() == "A");
		TEST_ASSERT(group.annotations[0]->GetArguments().Empty());

		TEST_ASSERT(group.annotations[1]->GetName() == "B");
		TEST_ASSERT(group.annotations[1]->GetArguments().Count() == 1);

		TEST_ASSERT(group.annotations[2]->GetName() == "C");
		TEST_ASSERT(group.annotations[2]->GetArguments().Empty());
		TEST_PASSED();
	}

	void AnnotationTest::TestConsecutiveBrackets()
	{
		auto stream = Scripting::Source::FromMemory("[A][B]");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::AnnotationNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetBracketGroups().Count() == 2);
		TEST_ASSERT(node->TotalAnnotationCount() == 2);

		TEST_ASSERT(node->GetBracketGroups()[0].annotations[0]->GetName() == "A");
		TEST_ASSERT(node->GetBracketGroups()[1].annotations[0]->GetName() == "B");
		TEST_PASSED();
	}

	void AnnotationTest::TestComplexExample()
	{
		auto stream = Scripting::Source::FromMemory("[Annotation, Test(\"stuff\")][AnotherAnnotation]");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::AnnotationNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetBracketGroups().Count() == 2);
		TEST_ASSERT(node->TotalAnnotationCount() == 3);

		const auto& group1 = node->GetBracketGroups()[0];
		TEST_ASSERT(group1.annotations.Count() == 2);
		TEST_ASSERT(group1.annotations[0]->GetName() == "Annotation");
		TEST_ASSERT(group1.annotations[1]->GetName() == "Test");
		TEST_ASSERT(group1.annotations[1]->GetArguments().Count() == 1);

		const auto& group2 = node->GetBracketGroups()[1];
		TEST_ASSERT(group2.annotations.Count() == 1);
		TEST_ASSERT(group2.annotations[0]->GetName() == "AnotherAnnotation");
		TEST_PASSED();
	}

	void AnnotationTest::TestQualifiedNameAnnotation()
	{
		auto stream = Scripting::Source::FromMemory("[NC::SomeAttr]");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::AnnotationNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->TotalAnnotationCount() == 1);

		const auto& group = node->GetBracketGroups()[0];
		auto item = group.annotations[0];
		TEST_ASSERT(item->GetName() == "NC::SomeAttr");
		TEST_ASSERT(item->GetNameTokenCount() == 2);
		TEST_PASSED();
	}

	void AnnotationTest::TestNoBracketReturnsNullptr()
	{
		auto stream = Scripting::Source::FromMemory("namespace Foo {}");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::AnnotationNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		TEST_PASSED();
	}

	void AnnotationTest::TestEmptyBrackets()
	{
		auto stream = Scripting::Source::FromMemory("[]");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::AnnotationNode::TryParse(state);
		// Empty brackets should return nullptr since no valid annotation was found
		TEST_ASSERT(node == nullptr);
		TEST_PASSED();
	}

	namespace
	{
		struct ParsedAnnotation
		{
			Scripting::ErrorTracker errors;
			Ref<Scripting::AnnotationNode> node;
		};

		void ParseAnnotation(const char* text, ParsedAnnotation& out)
		{
			auto stream = Scripting::Source::FromMemory(text);
			Scripting::Lexer lexer(*stream, 0);
			Scripting::ParserState state(lexer, out.errors);
			out.node = Scripting::AnnotationNode::TryParse(state);
		}

		const Scripting::AnnotationItem* FirstItem(const ParsedAnnotation& parsed)
		{
			if (!parsed.node || parsed.node->GetBracketGroups().Empty()) return nullptr;
			const auto& items = parsed.node->GetBracketGroups()[0].annotations;
			return items.Empty() ? nullptr : items[0].Get();
		}
	}

	void AnnotationTest::TestArgumentsAreExpressions()
	{
		ParsedAnnotation parsed;
		ParseAnnotation("[Range(-1, Limits::kMax * 2, \"label\")]", parsed);
		const Scripting::AnnotationItem* item = FirstItem(parsed);
		TEST_ASSERT(item != nullptr);
		TEST_ASSERT_EQ((int)parsed.errors.GetErrors().Count(), 0);
		TEST_ASSERT_EQ((int)item->GetArguments().Count(), 3);
		TEST_ASSERT(item->GetArguments()[0]->Type() != Scripting::NodeType::LiteralExpression); // `-1` is a unary expression
		TEST_ASSERT(item->GetArguments()[1]->Type() != Scripting::NodeType::LiteralExpression);
		TEST_ASSERT(item->GetArguments()[2]->Type() == Scripting::NodeType::LiteralExpression);
		TEST_PASSED();
	}

	void AnnotationTest::TestEmptyArgumentList()
	{
		ParsedAnnotation parsed;
		ParseAnnotation("[Obsolete()]", parsed);
		const Scripting::AnnotationItem* item = FirstItem(parsed);
		TEST_ASSERT(item != nullptr);
		TEST_ASSERT_EQ((int)parsed.errors.GetErrors().Count(), 0);
		TEST_ASSERT(item->GetArguments().Empty());
		TEST_PASSED();
	}

	void AnnotationTest::TestMissingArgumentIsAnError()
	{
		ParsedAnnotation parsed;
		ParseAnnotation("[Range(1, )]", parsed);
		TEST_ASSERT(parsed.errors.GetErrors().Count() >= 1);
		TEST_ASSERT(parsed.errors.GetErrors()[0].message.find("Expected annotation argument expression") != Core::String::npos);
		TEST_PASSED();
	}

	void AnnotationTest::TestCompilerAnnotationTakesLiterals()
	{
		ParsedAnnotation named;
		ParseAnnotation("[group(kName)]", named);
		TEST_ASSERT_EQ((int)named.errors.GetErrors().Count(), 1);
		TEST_ASSERT(named.errors.GetErrors()[0].message == "'group' takes string literal arguments");
		ParsedAnnotation literal;
		ParseAnnotation("[operator(\"+\", \"add\")]", literal);
		TEST_ASSERT_EQ((int)literal.errors.GetErrors().Count(), 0);
		TEST_PASSED();
	}

	void AnnotationTest::TestCompilerAnnotationNames()
	{
		for (const char* text : { "[operator(\"+\")]", "[test]", "[test_setup]", "[test_teardown]", "[group(\"g\")]", "[link(\"s\")]", "[uuid(\"u\")]" })
		{
			ParsedAnnotation parsed;
			ParseAnnotation(text, parsed);
			TEST_ASSERT(FirstItem(parsed) != nullptr && FirstItem(parsed)->IsCompilerAnnotation());
		}
		ParsedAnnotation user;
		ParseAnnotation("[Game::Serialize]", user);
		TEST_ASSERT(FirstItem(user) != nullptr && !FirstItem(user)->IsCompilerAnnotation());
		TEST_ASSERT(FirstItem(user)->GetName() == "Game::Serialize");
		TEST_PASSED();
	}

	void AnnotationTest::TestEnumKeepsAnnotations()
	{
		auto stream = Scripting::Source::FromMemory("[Flags, Tag(3)] public enum class Mode { A, B }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);
		auto node = Scripting::EnumDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT_EQ((int)node->GetAnnotations().Count(), 1);
		TEST_ASSERT_EQ((int)node->GetAnnotations()[0]->TotalAnnotationCount(), 2);
		TEST_PASSED();
	}

	void AnnotationTest::TestUnionKeepsAnnotations()
	{
		auto stream = Scripting::Source::FromMemory("[Packed] union Bits { int32 a; float b; }");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);
		auto node = Scripting::UnionDefinitionNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT_EQ((int)node->GetAnnotations().Count(), 1);
		TEST_PASSED();
	}
}
