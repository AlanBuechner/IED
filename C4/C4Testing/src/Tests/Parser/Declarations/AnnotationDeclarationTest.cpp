#include "AnnotationDeclarationTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
	namespace
	{
		struct Parsed
		{
			Ref<C4Compiler::AnnotationDeclarationNode> node;
			uint32 errors = 0;
			Core::String next; // the token after it
		};

		Parsed Parse(const char* text)
		{
			auto stream = C4Compiler::Source::FromMemory(text);
			C4Compiler::Lexer lexer(*stream, 0);
			C4Compiler::ErrorTracker errorTracker;
			C4Compiler::ParserState state(lexer, errorTracker);
			Parsed parsed;
			parsed.node = C4Compiler::AnnotationDeclarationNode::TryParse(state);
			parsed.errors = errorTracker.GetErrors().Count();
			parsed.next = lexer.PeekCompound().GetText();
			return parsed;
		}
	}

	void AnnotationDeclarationTest::ReadsTheNameParametersAndPlaces()
	{
		Parsed parsed = Parse("annotation Range(double min, double max = 1.0) on field, parameter; next");
		TEST_ASSERT(parsed.node != nullptr);
		TEST_ASSERT_EQ((int)parsed.errors, 0);
		const auto& node = *parsed.node;
		TEST_ASSERT(node.GetKeywordToken().GetText() == "annotation");
		TEST_ASSERT(node.GetName() == "Range");
		TEST_ASSERT_EQ((int)node.GetParameters().Count(), 2);
		TEST_ASSERT(node.GetParameters()[1]->GetNameToken().GetText() == "max" && node.GetParameters()[1]->HasDefaultValue());
		TEST_ASSERT(node.GetOnToken().GetText() == "on");
		TEST_ASSERT_EQ((int)node.GetPlaceTokens().Count(), 2);
		TEST_ASSERT(node.GetPlaceTokens()[0].GetText() == "field" && node.GetPlaceTokens()[1].GetText() == "parameter");
		TEST_ASSERT(node.GetSemicolonToken().GetText() == ";");
		TEST_ASSERT(node.startLocation.column == 1 && node.endLocation.byteOffset == node.GetSemicolonToken().GetEndLocation().byteOffset);
		TEST_ASSERT_EQ((int)node.Children().size(), 2); // its parameters, which the editor walks
		TEST_ASSERT(parsed.next == "next");
		TEST_PASSED();
	}

	void AnnotationDeclarationTest::TheParenthesesAndPlacesMayBeLeftOut()
	{
		Parsed component = Parse("annotation Component on type;");
		TEST_ASSERT(component.node && component.errors == 0);
		TEST_ASSERT(component.node->GetParameters().Empty() && component.node->GetPlaceTokens().Count() == 1);
		TEST_ASSERT(component.node->ToString() == "annotation Component on type");

		Parsed empty = Parse("annotation Marked() on method;");
		TEST_ASSERT(empty.node && empty.errors == 0 && empty.node->GetParameters().Empty());

		Parsed anywhere = Parse("annotation Tag(const char* name);");
		TEST_ASSERT(anywhere.node && anywhere.errors == 0);
		TEST_ASSERT(anywhere.node->GetOnToken().GetText().empty() && anywhere.node->GetPlaceTokens().Empty());

		Parsed pack = Parse("annotation Requires(typename component, typename... more) on type;");
		TEST_ASSERT(pack.node && pack.errors == 0 && pack.node->GetParameters()[1]->IsPack());
		TEST_PASSED();
	}

	void AnnotationDeclarationTest::APlaceMayBeAKeyword()
	{
		Parsed parsed = Parse("annotation Flags on enum, union;");
		TEST_ASSERT(parsed.node && parsed.errors == 0);
		TEST_ASSERT(parsed.node->GetPlaceTokens()[0].GetText() == "enum" && parsed.node->GetPlaceTokens()[1].GetText() == "union");
		TEST_PASSED();
	}

	void AnnotationDeclarationTest::AnnotationIsANameAnywhereElse()
	{
		for (const char* text : { "annotation = 3;", "annotation.m_Id = 1;", "annotation::Find();", "annotation(1);", "annotation;" })
		{
			Parsed parsed = Parse(text);
			TEST_ASSERT(parsed.node == nullptr);
			TEST_ASSERT(parsed.next == "annotation"); // nothing taken
		}
		TEST_PASSED();
	}
}
