#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Escape sequences (B2, BUG-017, D-02): the C set plus `\e`, `\x` capped at the code unit, octal,
	// `\u` / `\U` encoded in the literal's width, and an error for anything else.
	CLASS(GROUP = Testing) EscapeTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::EscapeTest);

		TEST_FUNC(Lexer.Escapes.Simple)
		void SimpleEscapes();
		TEST_FUNC(Lexer.Escapes.Hex)
		void HexEscapesTakeTwoDigitsPerByte();
		TEST_FUNC(Lexer.Escapes.Octal)
		void OctalEscapes();
		TEST_FUNC(Lexer.Escapes.Universal)
		void UniversalEscapesEncodeInTheLiteralsWidth();
		TEST_FUNC(Lexer.Escapes.Errors)
		void BadEscapesAreErrors();
	};
} // namespace Testing
