#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Character and string literals (B2, D-02): prefixes pick the code-unit width, a character literal holds
	// exactly one character that fits one unit, source UTF-8 is re-encoded for wide literals, and a literal
	// ends on its line.
	CLASS(GROUP = Testing) QuotedLiteralTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::QuotedLiteralTest);

		TEST_FUNC(Lexer.Quoted.Prefixes)
		void PrefixesPickTheWidth();
		TEST_FUNC(Lexer.Quoted.Prefixes)
		void NamesStartingWithUStayNames();
		TEST_FUNC(Lexer.Quoted.Characters)
		void CharacterLiteralsHoldOneCharacter();
		TEST_FUNC(Lexer.Quoted.Source)
		void SourceUtf8IsReencodedForWideLiterals();
		TEST_FUNC(Lexer.Quoted.Lines)
		void LiteralsEndOnTheirLine();
	};
} // namespace Testing
