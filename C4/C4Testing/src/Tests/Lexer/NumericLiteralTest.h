#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Numeric literals (B2, BUG-016): the spelling splits into radix, digits and suffix, every suffix form is
	// validated, and a bad literal is one token with a precise diagnostic.
	CLASS(GROUP = Testing) NumericLiteralTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::NumericLiteralTest);

		TEST_FUNC(Lexer.Numbers.Spelling)
		void SpellingSplitsRadixDigitsAndSuffix();
		TEST_FUNC(Lexer.Numbers.Spelling)
		void OperatorsAfterANumberStayOperators();
		TEST_FUNC(Lexer.Numbers.Suffixes)
		void EveryValidSuffixOnEveryRadix();
		TEST_FUNC(Lexer.Numbers.Suffixes)
		void InvalidSpellingsHaveOneIssue();
		TEST_FUNC(Lexer.Numbers.Diagnostics)
		void IssuesAreWordedAndReportedOnce();
	};
} // namespace Testing
