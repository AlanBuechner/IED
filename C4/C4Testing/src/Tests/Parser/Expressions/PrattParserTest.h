#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/PrattParser.h"
#include "Parser/AstNode.h"
#include "Parser/Nodes/Expressions/BinaryExpressionNode.h"
#include "Parser/Nodes/Expressions/UnaryExpressionNode.h"
#include "Parser/Nodes/Expressions/TernaryExpressionNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include "Parser/Nodes/Expressions/ArrayIndexNode.h"
#include "Parser/Nodes/Expressions/AssignmentExpressionNode.h"
#include "Parser/Nodes/Expressions/ConditionalAndNode.h"
#include "Parser/Nodes/Expressions/ConditionalOrNode.h"
#include "Parser/Nodes/Expressions/ParenthesizedExpressionNode.h"
#include "Parser/Nodes/Expressions/CastExpressionNode.h"

namespace Testing
{
	CLASS(GROUP = Testing) PrattParserTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::PrattParserTest);

		// --- Binary operator precedence ---
		TEST_FUNC(Parser.Pratt.Precedence.Binary)
		void TestSimpleAddition();

		TEST_FUNC(Parser.Pratt.Precedence.Binary)
		void TestMultiplicationBeforeAddition();

		TEST_FUNC(Parser.Pratt.Precedence.Binary)
		void TestRightAssociativePower();

		TEST_FUNC(Parser.Pratt.Precedence.Binary)
		void TestComparisonAndEqualityPrecedence();

		TEST_FUNC(Parser.Pratt.Precedence.Binary)
		void TestBitwisePrecedence();

		TEST_FUNC(Parser.Pratt.Precedence.Binary)
		void TestShiftOperators();

		// --- Unary operators ---
		TEST_FUNC(Parser.Pratt.Unary.Prefix)
		void TestPrefixNegation();

		TEST_FUNC(Parser.Pratt.Unary.Prefix)
		void TestDoubleNegation();

		TEST_FUNC(Parser.Pratt.Unary.Prefix)
		void TestPrefixLogicalNot();

		TEST_FUNC(Parser.Pratt.Unary.Postfix)
		void TestPostfixIncrement();

		TEST_FUNC(Parser.Pratt.Unary.Postfix)
		void TestPostfixDecrement();

		// --- Ternary ---
		TEST_FUNC(Parser.Pratt.Ternary.Basic)
		void TestSimpleTernary();

		TEST_FUNC(Parser.Pratt.Ternary.Nested)
		void TestNestedTernaryRightAssociative();

		// --- Function calls ---
		TEST_FUNC(Parser.Pratt.FunctionCalls.Basic)
		void TestEmptyCall();

		TEST_FUNC(Parser.Pratt.FunctionCalls.Basic)
		void TestSingleArgument();

		TEST_FUNC(Parser.Pratt.FunctionCalls.Basic)
		void TestMultipleArguments();

		TEST_FUNC(Parser.Pratt.FunctionCalls.Basic)
		void TestExpressionAsArgument();

		// --- Member access ---
		TEST_FUNC(Parser.Pratt.MemberAccess.Dot)
		void TestDotAccess();

		TEST_FUNC(Parser.Pratt.MemberAccess.Arrow)
		void TestArrowAccess();

		TEST_FUNC(Parser.Pratt.MemberAccess.Chain)
		void TestChainedDotAccess();

		// --- Static access ---
		TEST_FUNC(Parser.Pratt.StaticAccess.Basic)
		void TestStaticAccess();

		TEST_FUNC(Parser.Pratt.StaticAccess.Chain)
		void TestChainedStaticAccess();

		// --- Array indexing ---
		TEST_FUNC(Parser.Pratt.ArrayIndex.Basic)
		void TestSimpleIndex();

		TEST_FUNC(Parser.Pratt.ArrayIndex.Expression)
		void TestExpressionIndex();

		// --- Postfix chaining ---
		TEST_FUNC(Parser.Pratt.Postfix.Chain)
		void TestCallThenIndex();

		TEST_FUNC(Parser.Pratt.Postfix.Chain)
		void TestMemberThenCall();

		TEST_FUNC(Parser.Pratt.Postfix.Chain)
		void TestComplexPostfixChain();

		// --- Parenthesized expressions ---
		TEST_FUNC(Parser.Pratt.Parentheses.Basic)
		void TestSimpleParenthesized();

		TEST_FUNC(Parser.Pratt.Parentheses.Nested)
		void TestNestedParentheses();

		// --- Conditional and/or ---
		TEST_FUNC(Parser.Pratt.Conditional.AndOr)
		void TestConditionalAnd();

		TEST_FUNC(Parser.Pratt.Conditional.AndOr)
		void TestConditionalOr();

		TEST_FUNC(Parser.Pratt.Conditional.AndOr)
		void TestAndHigherThanOr();

		// --- Assignments ---
		TEST_FUNC(Parser.Pratt.Assignment.Basic)
		void TestSimpleAssignment();

		TEST_FUNC(Parser.Pratt.Assignment.Compound)
		void TestCompoundAssignment();

		// --- Casts ---
		TEST_FUNC(Parser.Pratt.Casts.CStyle)
		void TestCStyleCast();

		TEST_FUNC(Parser.Pratt.Casts.CppStyle)
		void TestStaticCast();

		// A cast is a primary expression: it must bind TIGHTER than every infix operator.
		// `(int)f + 1` must parse as `((int)f) + 1`, never `(int)(f + 1)`. These are the
		// regression guards for the minBindPower=0 bug that swallowed operators into casts.
		TEST_FUNC(Parser.Pratt.Casts.Precedence)
		void TestCStyleCastBindsTighterThanAddition();

		TEST_FUNC(Parser.Pratt.Casts.Precedence)
		void TestCStyleCastBindsTighterThanMultiplication();

		TEST_FUNC(Parser.Pratt.Casts.Precedence)
		void TestCStyleCastBindsTighterThanComparison();

		TEST_FUNC(Parser.Pratt.Casts.Precedence)
		void TestCStyleCastOfParenthesizedOperandKeepsWholeExpr();

		// --- Integration with ExpressionStatementNode ---
		TEST_FUNC(Parser.Integration.ExpressionStatement)
		void TestExpressionAsStatement();

		TEST_FUNC(Parser.Integration.ExpressionStatement)
		void TestComplexExpressionAsStatement();

		// --- CodeBlock integration ---
		TEST_FUNC(Parser.Integration.CodeBlock)
		void TestCodeBlockWithBinaryExpressions();

		TEST_FUNC(Parser.Integration.CodeBlock)
		void TestCodeBlockWithFunctionCalls();

		// --- Template argument disambiguation (< vs Less) ---
		TEST_FUNC(Parser.Pratt.Template.Disambiguation)
		void TestStaticAccessThenTemplate();

		TEST_FUNC(Parser.Pratt.Template.Disambiguation)
		void TestComparisonNotConfusedWithTemplate();

		TEST_FUNC(Parser.Pratt.Template.Disambiguation)
		void TestComparisonWithCallOnTheRight();

		TEST_FUNC(Parser.Pratt.Template.Disambiguation)
		void TestExplicitTemplateCall();

		// --- Parse depth limit (stack-overflow guard) ---
		TEST_FUNC(Parser.Pratt.DepthLimit.Basic)
		void TestDeepNestingIsRejected();

		// Block nesting is a separate recursive-descent path from expressions; deep `{ }` blocks used to
		// overflow the stack and crash the compiler. The block-depth cap must turn that into a clean error.
		TEST_FUNC(Parser.Pratt.DepthLimit.Blocks)
		void TestDeepBlockNestingIsRejected();
	};
}
