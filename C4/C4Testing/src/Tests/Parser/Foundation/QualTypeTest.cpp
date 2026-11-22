#include "QualTypeTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
	void QualTypeTest::TestPlainType()
	{
		auto stream = C4Compiler::Source::FromMemory("int");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::QualTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(!node->IsLeadingConst());
		TEST_ASSERT(node->BaseName() != nullptr);
		TEST_ASSERT(node->BaseName()->ToString() == "int");
		TEST_ASSERT(node->IndirectionCount() == 0);
		TEST_ASSERT(node->ToString() == "int");
		TEST_PASSED();
	}

	void QualTypeTest::TestLeadingConst()
	{
		auto stream = C4Compiler::Source::FromMemory("const int");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::QualTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsLeadingConst());
		TEST_ASSERT(node->GetLeadingConstToken().GetText() == "const");
		TEST_ASSERT(node->BaseName()->ToString() == "int");
		TEST_ASSERT(node->IndirectionCount() == 0);
		TEST_ASSERT(node->ToString() == "const int");
		TEST_PASSED();
	}

	void QualTypeTest::TestPointer()
	{
		auto stream = C4Compiler::Source::FromMemory("int*");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::QualTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(!node->IsLeadingConst());
		TEST_ASSERT(node->BaseName()->ToString() == "int");
		TEST_ASSERT(node->IndirectionCount() == 1);

		const auto& indirections = node->GetIndirections();
		TEST_ASSERT(indirections[0].indirectionToken == C4Compiler::TokenType::Star);
		TEST_ASSERT(!indirections[0].isConst);
		TEST_ASSERT(node->ToString() == "int*");
		TEST_PASSED();
	}

	void QualTypeTest::TestConstPointer()
	{
		auto stream = C4Compiler::Source::FromMemory("int const*");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::QualTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(!node->IsLeadingConst());
		TEST_ASSERT(node->BaseName()->ToString() == "int");
		TEST_ASSERT(node->IndirectionCount() == 1);

		const auto& indirections = node->GetIndirections();
		TEST_ASSERT(indirections[0].indirectionToken == C4Compiler::TokenType::Star);
		TEST_ASSERT(indirections[0].isConst);
		TEST_ASSERT(node->ToString() == "int const*");
		TEST_PASSED();
	}

	void QualTypeTest::TestPointerToConst()
	{
		auto stream = C4Compiler::Source::FromMemory("const int*");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::QualTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsLeadingConst());
		TEST_ASSERT(node->BaseName()->ToString() == "int");
		TEST_ASSERT(node->IndirectionCount() == 1);

		const auto& indirections = node->GetIndirections();
		TEST_ASSERT(indirections[0].indirectionToken == C4Compiler::TokenType::Star);
		TEST_ASSERT(!indirections[0].isConst); // leading const applies to base, not the pointer
		TEST_ASSERT(node->ToString() == "const int*");
		TEST_PASSED();
	}

	void QualTypeTest::TestGCRef()
	{
		// All pointers are GC-managed: `T*` is the only pointer type (the old `T^` syntax was removed).
		auto stream = C4Compiler::Source::FromMemory("String*");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::QualTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(!node->IsLeadingConst());
		TEST_ASSERT(node->BaseName()->ToString() == "String");
		TEST_ASSERT(node->IndirectionCount() == 1);

		const auto& indirections = node->GetIndirections();
		TEST_ASSERT(indirections[0].indirectionToken == C4Compiler::TokenType::Star);
		TEST_ASSERT(!indirections[0].isConst);
		TEST_ASSERT(node->ToString() == "String*");
		TEST_PASSED();
	}

	void QualTypeTest::TestCPPRef()
	{
		auto stream = C4Compiler::Source::FromMemory("int&");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::QualTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(!node->IsLeadingConst());
		TEST_ASSERT(node->BaseName()->ToString() == "int");
		TEST_ASSERT(node->IndirectionCount() == 1);

		const auto& indirections = node->GetIndirections();
		TEST_ASSERT(indirections[0].indirectionToken == C4Compiler::TokenType::Ampersand);
		TEST_ASSERT(!indirections[0].isConst);
		TEST_ASSERT(node->ToString() == "int&");
		TEST_PASSED();
	}

	void QualTypeTest::TestArray()
	{
		auto stream = C4Compiler::Source::FromMemory("int[10]");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::QualTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(!node->IsLeadingConst());
		TEST_ASSERT(node->BaseName()->ToString() == "int");
		TEST_ASSERT(node->IndirectionCount() == 1);

		const auto& indirections = node->GetIndirections();
		TEST_ASSERT(indirections[0].indirectionToken == C4Compiler::TokenType::OpenBracket);
		TEST_ASSERT(!indirections[0].isConst);
		TEST_ASSERT(indirections[0].arraySizeExpr != nullptr);
		TEST_ASSERT(indirections[0].arraySizeExpr->ToString() == "10");
		TEST_ASSERT(node->ToString() == "int[10]");
		TEST_PASSED();
	}

	void QualTypeTest::TestComplexChain()
	{
		auto stream = C4Compiler::Source::FromMemory("const int* const &");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::QualTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsLeadingConst());
		TEST_ASSERT(node->BaseName()->ToString() == "int");
		TEST_ASSERT(node->IndirectionCount() == 2); // *, &

		const auto& indirections = node->GetIndirections();
		// Layer 0: * (no const)
		TEST_ASSERT(indirections[0].indirectionToken == C4Compiler::TokenType::Star);
		TEST_ASSERT(!indirections[0].isConst);
		// Layer 1: const & (const applies to this layer)
		TEST_ASSERT(indirections[1].indirectionToken == C4Compiler::TokenType::Ampersand);
		TEST_ASSERT(indirections[1].isConst);

		TEST_ASSERT(node->ToString() == "const int* const&");
		TEST_PASSED();
	}

	void QualTypeTest::TestQualifiedNameWithTemplateArgs()
	{
		auto stream = C4Compiler::Source::FromMemory("NC::SomeClass<float>");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::QualTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(!node->IsLeadingConst());
		TEST_ASSERT(node->BaseName() != nullptr);
		TEST_ASSERT(node->BaseName()->SegmentCount() == 2);
		TEST_ASSERT(node->BaseName()->GetSegments()[0].nameToken.GetText() == "NC");
		TEST_ASSERT(node->BaseName()->GetSegments()[1].nameToken.GetText() == "SomeClass");

		auto templateArgs = node->BaseName()->GetSegments()[1].templateArgs;
		TEST_ASSERT(templateArgs != nullptr);
		TEST_ASSERT(templateArgs->ArgumentCount() == 1);

		TEST_ASSERT(node->IndirectionCount() == 0);
		TEST_ASSERT(node->ToString() == "NC::SomeClass<float>");
		TEST_PASSED();
	}

	void QualTypeTest::TestFullComplex()
	{
		auto stream = C4Compiler::Source::FromMemory("const NC::SomeClass<float>::SubType&");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::QualTypeNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->IsLeadingConst());

		// Base name: NC::SomeClass<float>::SubType
		TEST_ASSERT(node->BaseName() != nullptr);
		TEST_ASSERT(node->BaseName()->SegmentCount() == 3);
		TEST_ASSERT(node->BaseName()->GetSegments()[0].nameToken.GetText() == "NC");
		TEST_ASSERT(node->BaseName()->GetSegments()[1].nameToken.GetText() == "SomeClass");
		TEST_ASSERT(node->BaseName()->GetSegments()[2].nameToken.GetText() == "SubType");

		auto templateArgs = node->BaseName()->GetSegments()[1].templateArgs;
		TEST_ASSERT(templateArgs != nullptr);
		TEST_ASSERT(templateArgs->ArgumentCount() == 1);

		// Indirection: &
		TEST_ASSERT(node->IndirectionCount() == 1);
		const auto& indirections = node->GetIndirections();
		TEST_ASSERT(indirections[0].indirectionToken == C4Compiler::TokenType::Ampersand);
		TEST_ASSERT(!indirections[0].isConst);

		TEST_ASSERT(node->ToString() == "const NC::SomeClass<float>::SubType&");
		TEST_PASSED();
	}

	void QualTypeTest::TestNonTypeTokenReturnsNullptr()
	{
		auto stream = C4Compiler::Source::FromMemory("+ x");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::QualTypeNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed - the '+' is still available
		TEST_ASSERT(lexer.Peek().GetType() == C4Compiler::TokenType::Plus);
		TEST_PASSED();
	}

	void QualTypeTest::TestMissingTypeNameAfterConst()
	{
		auto stream = C4Compiler::Source::FromMemory("const +");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::QualTypeNode::TryParse(state);
		// Should return nullptr because no valid base name after const
		TEST_ASSERT(node == nullptr);
		TEST_PASSED();
	}
}
