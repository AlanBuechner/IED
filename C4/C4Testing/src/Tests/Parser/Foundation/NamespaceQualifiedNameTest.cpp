#include "NamespaceQualifiedNameTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"

namespace Testing
{
	void NamespaceQualifiedNameTest::TestSingleIdentifier()
	{
		auto stream = Scripting::Source::FromMemory("SomeClass");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceQualifiedNameNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->SegmentCount() == 1);
		TEST_ASSERT(node->GetSegments()[0].nameToken.GetText() == "SomeClass");
		TEST_ASSERT(node->GetSegments()[0].templateArgs == nullptr);
		TEST_ASSERT(node->ToString() == "SomeClass");
		TEST_PASSED();
	}

	void NamespaceQualifiedNameTest::TestTwoSegments()
	{
		auto stream = Scripting::Source::FromMemory("NC::SomeClass");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceQualifiedNameNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->SegmentCount() == 2);
		TEST_ASSERT(node->GetSegments()[0].nameToken.GetText() == "NC");
		TEST_ASSERT(node->GetSegments()[1].nameToken.GetText() == "SomeClass");
		TEST_ASSERT(node->GetSegments()[0].templateArgs == nullptr);
		TEST_ASSERT(node->GetSegments()[1].templateArgs == nullptr);
		TEST_ASSERT(node->ToString() == "NC::SomeClass");
		TEST_PASSED();
	}

	void NamespaceQualifiedNameTest::TestWithTemplateArgsOnOneSegment()
	{
		auto stream = Scripting::Source::FromMemory("NC::SomeClass<float>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceQualifiedNameNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->SegmentCount() == 2);
		TEST_ASSERT(node->GetSegments()[0].nameToken.GetText() == "NC");
		TEST_ASSERT(node->GetSegments()[0].templateArgs == nullptr);
		TEST_ASSERT(node->GetSegments()[1].nameToken.GetText() == "SomeClass");

		auto templateArgs = node->GetSegments()[1].templateArgs;
		TEST_ASSERT(templateArgs != nullptr);
		TEST_ASSERT(templateArgs->ArgumentCount() == 1);
		// Template argument is a QualType wrapping the base name "float"
		auto arg0 = templateArgs->GetArguments()[0];
		TEST_ASSERT(arg0->Type() == Scripting::NodeType::QualType);
		auto qualType = arg0.As<Scripting::QualTypeNode>();
		TEST_ASSERT(qualType->BaseName()->ToString() == "float");
		TEST_ASSERT(qualType->IndirectionCount() == 0);

		TEST_ASSERT(node->ToString() == "NC::SomeClass<float>");
		TEST_PASSED();
	}

	void NamespaceQualifiedNameTest::TestNestedTemplateArgs()
	{
		auto stream = Scripting::Source::FromMemory("NC::SomeClass<NC::Other<int>>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceQualifiedNameNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->SegmentCount() == 2);

		auto templateArgs = node->GetSegments()[1].templateArgs;
		TEST_ASSERT(templateArgs != nullptr);
		TEST_ASSERT(templateArgs->ArgumentCount() == 1);

		// Top-level template arg is a QualType wrapping NC::Other<int>
		auto arg0 = templateArgs->GetArguments()[0];
		TEST_ASSERT(arg0->Type() == Scripting::NodeType::QualType);
		auto qualType = arg0.As<Scripting::QualTypeNode>();
		auto baseName = qualType->BaseName();
		TEST_ASSERT(baseName->SegmentCount() == 2);
		TEST_ASSERT(baseName->GetSegments()[0].nameToken.GetText() == "NC");
		TEST_ASSERT(baseName->GetSegments()[1].nameToken.GetText() == "Other");

		// Nested template args on the inner segment
		auto nestedTemplateArgs = baseName->GetSegments()[1].templateArgs;
		TEST_ASSERT(nestedTemplateArgs != nullptr);
		TEST_ASSERT(nestedTemplateArgs->ArgumentCount() == 1);

		TEST_ASSERT(node->ToString() == "NC::SomeClass<NC::Other<int>>");
		TEST_PASSED();
	}

	void NamespaceQualifiedNameTest::TestComplexChain()
	{
		auto stream = Scripting::Source::FromMemory("NC::SomeClass<float>::SubType<NC::Other<int>>::Type3");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceQualifiedNameNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->SegmentCount() == 4);

		// Segment 0: NC (no template args)
		TEST_ASSERT(node->GetSegments()[0].nameToken.GetText() == "NC");
		TEST_ASSERT(node->GetSegments()[0].templateArgs == nullptr);

		// Segment 1: SomeClass<float>
		TEST_ASSERT(node->GetSegments()[1].nameToken.GetText() == "SomeClass");
		TEST_ASSERT(node->GetSegments()[1].templateArgs != nullptr);
		TEST_ASSERT(node->GetSegments()[1].templateArgs->ArgumentCount() == 1);

		// Segment 2: SubType<NC::Other<int>>
		TEST_ASSERT(node->GetSegments()[2].nameToken.GetText() == "SubType");
		TEST_ASSERT(node->GetSegments()[2].templateArgs != nullptr);
		TEST_ASSERT(node->GetSegments()[2].templateArgs->ArgumentCount() == 1);

		auto nestedArg = node->GetSegments()[2].templateArgs->GetArguments()[0];
		TEST_ASSERT(nestedArg->Type() == Scripting::NodeType::QualType);
		auto nestedQual = nestedArg.As<Scripting::QualTypeNode>();
		TEST_ASSERT(nestedQual->BaseName()->SegmentCount() == 2);

		// Segment 3: Type3 (no template args)
		TEST_ASSERT(node->GetSegments()[3].nameToken.GetText() == "Type3");
		TEST_ASSERT(node->GetSegments()[3].templateArgs == nullptr);

		TEST_ASSERT(node->ToString() == "NC::SomeClass<float>::SubType<NC::Other<int>>::Type3");
		TEST_PASSED();
	}

	void NamespaceQualifiedNameTest::TestTemplateArgWithConstPointer()
	{
		auto stream = Scripting::Source::FromMemory("NS::SomeClass<float const*>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceQualifiedNameNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->SegmentCount() == 2);

		auto templateArgs = node->GetSegments()[1].templateArgs;
		TEST_ASSERT(templateArgs != nullptr);
		TEST_ASSERT(templateArgs->ArgumentCount() == 1);

		auto arg0 = templateArgs->GetArguments()[0];
		TEST_ASSERT(arg0->Type() == Scripting::NodeType::QualType);
		auto qualType = arg0.As<Scripting::QualTypeNode>();
		TEST_ASSERT(qualType->BaseName()->ToString() == "float");
		TEST_ASSERT(!qualType->IsLeadingConst());

		TEST_ASSERT(qualType->IndirectionCount() == 1);
		const auto& indirections = qualType->GetIndirections();
		TEST_ASSERT(indirections[0].isConst);
		TEST_ASSERT(indirections[0].indirectionToken == Scripting::TokenType::Star);

		TEST_ASSERT(node->ToString() == "NS::SomeClass<float const*>");
		TEST_PASSED();
	}

	void NamespaceQualifiedNameTest::TestTemplateArgWithNestedTypePointerRef()
	{
		auto stream = Scripting::Source::FromMemory("NS::SomeClass<SomClass<int*>*&>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceQualifiedNameNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->SegmentCount() == 2);

		auto templateArgs = node->GetSegments()[1].templateArgs;
		TEST_ASSERT(templateArgs != nullptr);
		TEST_ASSERT(templateArgs->ArgumentCount() == 1);

		auto arg0 = templateArgs->GetArguments()[0];
		TEST_ASSERT(arg0->Type() == Scripting::NodeType::QualType);
		auto qualType = arg0.As<Scripting::QualTypeNode>();

		// Base name: SomClass<int*>
		TEST_ASSERT(qualType->BaseName()->ToString() == "SomClass<int*>");
		TEST_ASSERT(qualType->BaseName()->SegmentCount() == 1);
		TEST_ASSERT(qualType->BaseName()->GetSegments()[0].nameToken.GetText() == "SomClass");

		// Inner template arg: int* (QualType with one indirection layer = Star)
		auto innerTemplateArgs = qualType->BaseName()->GetSegments()[0].templateArgs;
		TEST_ASSERT(innerTemplateArgs != nullptr);
		TEST_ASSERT(innerTemplateArgs->ArgumentCount() == 1);

		auto innerArg = innerTemplateArgs->GetArguments()[0];
		TEST_ASSERT(innerArg->Type() == Scripting::NodeType::QualType);
		auto innerQual = innerArg.As<Scripting::QualTypeNode>();
		TEST_ASSERT(innerQual->BaseName()->ToString() == "int");
		TEST_ASSERT(innerQual->IndirectionCount() == 1);
		TEST_ASSERT(innerQual->GetIndirections()[0].indirectionToken == Scripting::TokenType::Star);

		// Outer indirection on SomClass<int*>: *&
		TEST_ASSERT(qualType->IndirectionCount() == 2);
		const auto& indirections = qualType->GetIndirections();
		TEST_ASSERT(indirections[0].indirectionToken == Scripting::TokenType::Star);
		TEST_ASSERT(!indirections[0].isConst);
		TEST_ASSERT(indirections[1].indirectionToken == Scripting::TokenType::Ampersand);
		TEST_ASSERT(!indirections[1].isConst);

		TEST_ASSERT(node->ToString() == "NS::SomeClass<SomClass<int*>*&>");
		TEST_PASSED();
	}

	void NamespaceQualifiedNameTest::TestMultipleTemplateArgsWithQualTypes()
	{
		auto stream = Scripting::Source::FromMemory("NS::Map<const Key*, Value* const&>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceQualifiedNameNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->SegmentCount() == 2);

		auto templateArgs = node->GetSegments()[1].templateArgs;
		TEST_ASSERT(templateArgs != nullptr);
		TEST_ASSERT(templateArgs->ArgumentCount() == 2);

		// First arg: const Key*
		auto arg0 = templateArgs->GetArguments()[0];
		TEST_ASSERT(arg0->Type() == Scripting::NodeType::QualType);
		auto qual0 = arg0.As<Scripting::QualTypeNode>();
		TEST_ASSERT(qual0->IsLeadingConst());
		TEST_ASSERT(qual0->BaseName()->ToString() == "Key");
		TEST_ASSERT(qual0->IndirectionCount() == 1);
		TEST_ASSERT(qual0->GetIndirections()[0].indirectionToken == Scripting::TokenType::Star);

		// Second arg: Value* const&
		auto arg1 = templateArgs->GetArguments()[1];
		TEST_ASSERT(arg1->Type() == Scripting::NodeType::QualType);
		auto qual1 = arg1.As<Scripting::QualTypeNode>();
		TEST_ASSERT(!qual1->IsLeadingConst());
		TEST_ASSERT(qual1->BaseName()->ToString() == "Value");
		TEST_ASSERT(qual1->IndirectionCount() == 2);
		const auto& indirections = qual1->GetIndirections();
		TEST_ASSERT(indirections[0].indirectionToken == Scripting::TokenType::Star);
		TEST_ASSERT(!indirections[0].isConst);
		TEST_ASSERT(indirections[1].indirectionToken == Scripting::TokenType::Ampersand);
		TEST_ASSERT(indirections[1].isConst);

		TEST_ASSERT(node->ToString() == "NS::Map<const Key*, Value* const&>");
		TEST_PASSED();
	}

	void NamespaceQualifiedNameTest::TestTemplateArgWithQualTypeAndNestedTemplates()
	{
		auto stream = Scripting::Source::FromMemory("NS::Container<NS::Vector<float const*> const*>");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceQualifiedNameNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->SegmentCount() == 2);

		auto templateArgs = node->GetSegments()[1].templateArgs;
		TEST_ASSERT(templateArgs != nullptr);
		TEST_ASSERT(templateArgs->ArgumentCount() == 1);

		auto arg0 = templateArgs->GetArguments()[0];
		TEST_ASSERT(arg0->Type() == Scripting::NodeType::QualType);
		auto outerQual = arg0.As<Scripting::QualTypeNode>();

		// Base name: NS::Vector<float const*>
		TEST_ASSERT(outerQual->BaseName()->SegmentCount() == 2);
		TEST_ASSERT(outerQual->BaseName()->GetSegments()[0].nameToken.GetText() == "NS");
		TEST_ASSERT(outerQual->BaseName()->GetSegments()[1].nameToken.GetText() == "Vector");

		// Inner template arg: float const*
		auto innerTemplateArgs = outerQual->BaseName()->GetSegments()[1].templateArgs;
		TEST_ASSERT(innerTemplateArgs != nullptr);
		TEST_ASSERT(innerTemplateArgs->ArgumentCount() == 1);

		auto innerArg = innerTemplateArgs->GetArguments()[0];
		TEST_ASSERT(innerArg->Type() == Scripting::NodeType::QualType);
		auto innerQual = innerArg.As<Scripting::QualTypeNode>();
		TEST_ASSERT(innerQual->BaseName()->ToString() == "float");
		TEST_ASSERT(innerQual->IndirectionCount() == 1);
		TEST_ASSERT(innerQual->GetIndirections()[0].isConst);
		TEST_ASSERT(innerQual->GetIndirections()[0].indirectionToken == Scripting::TokenType::Star);

		// Outer indirection: const*
		TEST_ASSERT(outerQual->IndirectionCount() == 1);
		TEST_ASSERT(outerQual->GetIndirections()[0].isConst);
		TEST_ASSERT(outerQual->GetIndirections()[0].indirectionToken == Scripting::TokenType::Star);

		TEST_ASSERT(node->ToString() == "NS::Container<NS::Vector<float const*> const*>");
		TEST_PASSED();
	}

	void NamespaceQualifiedNameTest::TestNonIdentifierStartsReturnsNullptr()
	{
		auto stream = Scripting::Source::FromMemory("123::Foo");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceQualifiedNameNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed -" the number token is still available
		TEST_ASSERT(lexer.Peek().GetType() == Scripting::TokenType::Number);
		TEST_PASSED();
	}

	void NamespaceQualifiedNameTest::TestMissingIdentifierAfterScopeResolution()
	{
		auto stream = Scripting::Source::FromMemory("NC::123");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceQualifiedNameNode::TryParse(state);
		// Returns partial node with what was parsed before the error
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->SegmentCount() == 1);
		TEST_ASSERT(node->GetSegments()[0].nameToken.GetText() == "NC");

		// Error should be recorded for missing identifier
		TEST_ASSERT(errorTracker.GetErrors().Count() >= 1);
		TEST_PASSED();
	}

	void NamespaceQualifiedNameTest::TestMissingCloseAngleInTemplateArgs()
	{
		auto stream = Scripting::Source::FromMemory("NC::Foo<int;");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::NamespaceQualifiedNameNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->SegmentCount() == 2);

		auto templateArgs = node->GetSegments()[1].templateArgs;
		TEST_ASSERT(templateArgs != nullptr);
		// Should have recovered: argument parsed, error recorded for missing '>'
		TEST_ASSERT(errorTracker.GetErrors().Count() >= 1);
		TEST_PASSED();
	}
}
