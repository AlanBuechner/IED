#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Pack expansions, fold expressions and `sizeof...` (docs/language/08 6, D-19), and member template calls
	// with written arguments (BUG-117), as the expression parser builds them.
	CLASS(GROUP = Testing) PackExpansionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::PackExpansionTest);

		TEST_FUNC(Parser.Pratt.Packs)
		void ExpansionInCallArguments();
		TEST_FUNC(Parser.Pratt.Packs)
		void PatternExpansion();
		TEST_FUNC(Parser.Pratt.Packs)
		void ExpansionInBracedList();
		TEST_FUNC(Parser.Pratt.Packs)
		void UnaryFolds();
		TEST_FUNC(Parser.Pratt.Packs)
		void BinaryFolds();
		TEST_FUNC(Parser.Pratt.Packs)
		void CommaFold();
		TEST_FUNC(Parser.Pratt.Packs)
		void ParenthesesWithoutEllipsisAreNoFold();
		TEST_FUNC(Parser.Pratt.Packs)
		void SizeOfPack();
		TEST_FUNC(Parser.Pratt.Packs)
		void MemberTemplateCallWithArguments();
		TEST_FUNC(Parser.Pratt.Packs)
		void MemberComparisonIsNoCall();
	};
} // namespace Testing
