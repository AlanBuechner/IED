#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) IdentifierExpressionTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::IdentifierExpressionTest);

		TEST_FUNC(Parser.Expressions.Identifier.Basic)
		void TestSimpleIdentifier();

		TEST_FUNC(Parser.Expressions.Identifier.Basic)
		void TestIdentifierAfterOperator();

		TEST_FUNC(Parser.Expressions.Identifier.Negative)
		void TestNumberLiteralReturnsNullptr();

		TEST_FUNC(Parser.Expressions.Identifier.Negative)
		void TestKeywordClassReturnsNullptr();
	};
}
