#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Operator precedence and associativity (BUG-001, D-27, docs/language/10 1): every pair of adjacent levels
	// in both orders, the right-associative operators, `**` against the prefix operators, and the C++ forms the
	// old table parsed differently.
	CLASS(GROUP = Testing) PrecedenceTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::PrecedenceTest);

		TEST_FUNC(Parser.Pratt.Precedence.Levels)
		void AdjacentLevelsBothOrders();
		TEST_FUNC(Parser.Pratt.Precedence.Levels)
		void TypeOperatorsAreRelational();
		TEST_FUNC(Parser.Pratt.Precedence.Associativity)
		void RightAssociativeOperators();
		TEST_FUNC(Parser.Pratt.Precedence.Associativity)
		void LeftAssociativeOperators();
		TEST_FUNC(Parser.Pratt.Precedence.Power)
		void PowerAgainstPrefixOperators();
		TEST_FUNC(Parser.Pratt.Precedence.Assignment)
		void AssignmentTakesTheWholeRightSide();
		TEST_FUNC(Parser.Pratt.Precedence.Assignment)
		void ConditionalBranches();
	};
} // namespace Testing
