#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) LiteralTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LiteralTest);

		TEST_FUNC(Parser.Expressions.Literal.Integer)
		void TestIntegerLiteral();

		TEST_FUNC(Parser.Expressions.Literal.Float)
		void TestFloatLiteral();

		TEST_FUNC(Parser.Expressions.Literal.String)
		void TestStringLiteral();

		TEST_FUNC(Parser.Expressions.Literal.Boolean)
		void TestBooleanTrue();

		TEST_FUNC(Parser.Expressions.Literal.Boolean)
		void TestBooleanFalse();

		TEST_FUNC(Parser.Expressions.Literal.Null)
		void TestNullLiteral();

		TEST_FUNC(Parser.Expressions.Literal.Negative)
		void TestIdentifierReturnsNullptr();
	};
}
