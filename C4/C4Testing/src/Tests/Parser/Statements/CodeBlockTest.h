#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) CodeBlockTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::CodeBlockTest);

		TEST_FUNC(Parser.Statements.CodeBlock.Empty)
		void TestEmptyBlock();

		TEST_FUNC(Parser.Statements.CodeBlock.SingleStatement)
		void TestSingleExpressionStatement();

		TEST_FUNC(Parser.Statements.CodeBlock.SingleLiteral)
		void TestSingleLiteralStatement();

		TEST_FUNC(Parser.Statements.CodeBlock.MultipleStatements)
		void TestMultipleStatements();

		TEST_FUNC(Parser.Statements.CodeBlock.Nested)
		void TestNestedBlock();

		TEST_FUNC(Parser.Statements.CodeBlock.Negative)
		void TestNoOpenBraceReturnsNullptr();
	};
}
