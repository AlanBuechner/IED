#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Statements/ExpressionStatementNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) ExpressionStatementTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ExpressionStatementTest);

		/// Identifier expression + semicolon (function call syntax requires PrattParser)
		TEST_FUNC(Parser.Statements.ExpressionStatement.Identifier)
		void TestIdentifierExpression();

		/// Literal expression + semicolon (assignment requires PrattParser)
		TEST_FUNC(Parser.Statements.ExpressionStatement.Literal)
		void TestLiteralExpression();

		/// String literal + semicolon
		TEST_FUNC(Parser.Statements.ExpressionStatement.StringLiteral)
		void TestStringLiteral();

		/// Expression without trailing semicolon must not match
		TEST_FUNC(Parser.Statements.ExpressionStatement.Negative)
		void TestNoSemicolonReturnsNullptr();

		/// Non-expression token at start must not match
		TEST_FUNC(Parser.Statements.ExpressionStatement.Negative)
		void TestNonExpressionStartsReturnsNullptr();
	};
}
