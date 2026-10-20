#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// #if / #elif conditions (B4, BUG-020): every operator and its precedence, `defined` in both forms,
	// expansion before evaluation, short-circuiting, and the errors.
	CLASS(GROUP = Testing) MacroExpressionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::MacroExpressionTest);

		TEST_FUNC(Macro.Expression.Operators)
		void EveryOperator();
		TEST_FUNC(Macro.Expression.Operators)
		void PrecedenceAndAssociativity();
		TEST_FUNC(Macro.Expression.Names)
		void DefinedAndMacroValues();
		TEST_FUNC(Macro.Expression.Names)
		void LiteralsInConditions();
		TEST_FUNC(Macro.Expression.Evaluation)
		void SkippedOperandsAreNotEvaluated();
		TEST_FUNC(Macro.Expression.Errors)
		void MalformedConditionsAreErrors();
	};
} // namespace Testing
