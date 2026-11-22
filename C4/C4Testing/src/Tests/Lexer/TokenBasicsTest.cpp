#include "TokenBasicsTest.h"

namespace Testing
{
	void TokenBasicsTest::TestDefaultConstruction()
	{
		C4Compiler::Token token;
		TEST_ASSERT(token.GetType() == C4Compiler::TokenType::None);
		TEST_PASSED();
	}

	void TokenBasicsTest::TestTypeConstructor()
	{
		C4Compiler::Token token(C4Compiler::TokenType::Identifier);
		TEST_ASSERT(token.GetType() == C4Compiler::TokenType::Identifier);
		TEST_PASSED();
	}

	void TokenBasicsTest::TestSourceLocationDefaults()
	{
		C4Compiler::Token token;
		const auto& loc = token.GetStartLocation();
		TEST_ASSERT(loc.line == 0);
		TEST_ASSERT(loc.column == 0);
		TEST_ASSERT(loc.byteOffset == 0);
		TEST_PASSED();
	}

	void TokenBasicsTest::TestSetStartEndLocation()
	{
		C4Compiler::Token token;
		C4Compiler::SourceLocation start{ 5, 10, 42 };
		C4Compiler::SourceLocation end{ 5, 15, 47 };
		token.SetStartLocation(start);
		token.SetEndLocation(end);

		const auto& s = token.GetStartLocation();
		TEST_ASSERT(s.line == 5);
		TEST_ASSERT(s.column == 10);
		TEST_ASSERT(s.byteOffset == 42);

		const auto& e = token.GetEndLocation();
		TEST_ASSERT(e.line == 5);
		TEST_ASSERT(e.column == 15);
		TEST_ASSERT(e.byteOffset == 47);

		TEST_PASSED();
	}

	void TokenBasicsTest::TestEmptyTokenText()
	{
		C4Compiler::Token token;
		const auto& text = token.GetText();
		TEST_ASSERT(text.empty());
		TEST_PASSED();
	}
}
