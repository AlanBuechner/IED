#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// sizeof / alignof / offsetof / __uuidof / sizeof... (B5): which operand each form records (a type, an
	// expression, or both for a parenthesized name), how sizeof binds, and the parse errors.
	CLASS(GROUP = Testing) CompileTimeQueryTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::CompileTimeQueryTest);

		TEST_FUNC(Parser.Expressions.CompileTimeQuery)
		void OperandForms();
		TEST_FUNC(Parser.Expressions.CompileTimeQuery)
		void OffsetofPathsAndPacks();
		TEST_FUNC(Parser.Expressions.CompileTimeQuery)
		void SizeofBindsLikeAPrefixOperator();
		TEST_FUNC(Parser.Expressions.CompileTimeQuery)
		void MalformedQueriesAreErrors();
	};
} // namespace Testing
