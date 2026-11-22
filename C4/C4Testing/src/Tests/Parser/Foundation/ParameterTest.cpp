#include "ParameterTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"

namespace Testing
{
	void ParameterTest::TestBasicParameter()
	{
		auto stream = C4Compiler::Source::FromMemory("int x");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ParameterNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetType() != nullptr);
		TEST_ASSERT(node->GetType()->BaseName()->ToString() == "int");
		TEST_ASSERT(node->HasName());
		TEST_ASSERT(node->GetNameToken().GetText() == "x");
		TEST_ASSERT(!node->HasDefaultValue());
		TEST_PASSED();
	}

	void ParameterTest::TestParameterWithDefault()
	{
		auto stream = C4Compiler::Source::FromMemory("float f = 0.0f");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ParameterNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetType() != nullptr);
		TEST_ASSERT(node->GetType()->BaseName()->ToString() == "float");
		TEST_ASSERT(node->HasName());
		TEST_ASSERT(node->GetNameToken().GetText() == "f");
		TEST_ASSERT(node->HasDefaultValue());
		TEST_PASSED();
	}

	void ParameterTest::TestUnnamedParameter()
	{
		auto stream = C4Compiler::Source::FromMemory("int");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ParameterNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetType() != nullptr);
		TEST_ASSERT(node->GetType()->BaseName()->ToString() == "int");
		TEST_ASSERT(!node->HasName());
		TEST_ASSERT(!node->HasDefaultValue());
		TEST_PASSED();
	}

	void ParameterTest::TestAnnotatedParameter()
	{
		auto stream = C4Compiler::Source::FromMemory("[In] int x");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ParameterNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetAnnotations().Count() == 1);
		TEST_ASSERT(node->GetType() != nullptr);
		TEST_ASSERT(node->GetType()->BaseName()->ToString() == "int");
		TEST_ASSERT(node->HasName());
		TEST_ASSERT(node->GetNameToken().GetText() == "x");
		TEST_PASSED();
	}

	void ParameterTest::TestComplexTypeWithDefault()
	{
		auto stream = C4Compiler::Source::FromMemory("const int* ptr = null");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ParameterNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetType() != nullptr);
		TEST_ASSERT(node->GetType()->IsLeadingConst());
		TEST_ASSERT(node->GetType()->BaseName()->ToString() == "int");
		TEST_ASSERT(node->GetType()->IndirectionCount() == 1);
		TEST_ASSERT(node->HasName());
		TEST_ASSERT(node->GetNameToken().GetText() == "ptr");
		TEST_ASSERT(node->HasDefaultValue());
		TEST_PASSED();
	}

	void ParameterTest::TestMultipleAnnotations()
	{
		auto stream = C4Compiler::Source::FromMemory("[A][B] String& s");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ParameterNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetAnnotations().Count() == 2);
		TEST_ASSERT(node->GetType() != nullptr);
		TEST_ASSERT(node->GetType()->BaseName()->ToString() == "String");
		TEST_ASSERT(node->GetType()->IndirectionCount() == 1);
		TEST_ASSERT(node->HasName());
		TEST_ASSERT(node->GetNameToken().GetText() == "s");
		TEST_PASSED();
	}

	void ParameterTest::TestNonTypeTokenReturnsNullptr()
	{
		auto stream = C4Compiler::Source::FromMemory("+ x");
		C4Compiler::Lexer lexer(*stream, 0);
		C4Compiler::ErrorTracker errorTracker;
		C4Compiler::ParserState state(lexer, errorTracker);

		auto node = C4Compiler::ParameterNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed - the '+' is still available
		TEST_ASSERT(lexer.Peek().GetType() == C4Compiler::TokenType::Plus);
		TEST_PASSED();
	}
}
