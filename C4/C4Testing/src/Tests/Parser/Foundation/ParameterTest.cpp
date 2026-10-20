#include "ParameterTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"

namespace Testing
{
	void ParameterTest::TestBasicParameter()
	{
		auto stream = Scripting::Source::FromMemory("int x");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ParameterNode::TryParse(state);
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
		auto stream = Scripting::Source::FromMemory("float f = 0.0f");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ParameterNode::TryParse(state);
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
		auto stream = Scripting::Source::FromMemory("int");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ParameterNode::TryParse(state);
		TEST_ASSERT(node != nullptr);
		TEST_ASSERT(node->GetType() != nullptr);
		TEST_ASSERT(node->GetType()->BaseName()->ToString() == "int");
		TEST_ASSERT(!node->HasName());
		TEST_ASSERT(!node->HasDefaultValue());
		TEST_PASSED();
	}

	void ParameterTest::TestAnnotatedParameter()
	{
		auto stream = Scripting::Source::FromMemory("[In] int x");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ParameterNode::TryParse(state);
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
		auto stream = Scripting::Source::FromMemory("const int* ptr = null");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ParameterNode::TryParse(state);
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
		auto stream = Scripting::Source::FromMemory("[A][B] String& s");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ParameterNode::TryParse(state);
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
		auto stream = Scripting::Source::FromMemory("+ x");
		Scripting::Lexer lexer(*stream, 0);
		Scripting::ErrorTracker errorTracker;
		Scripting::ParserState state(lexer, errorTracker);

		auto node = Scripting::ParameterNode::TryParse(state);
		TEST_ASSERT(node == nullptr);
		// No tokens consumed - the '+' is still available
		TEST_ASSERT(lexer.Peek().GetType() == Scripting::TokenType::Plus);
		TEST_PASSED();
	}
}
