#include "PrattParserTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/Nodes/Statements/ExpressionStatementNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <memory>

namespace Testing
{
	template<typename T>
	static Ref<T> CastNode(const Ref<C4Compiler::AstNode>& node)
	{
		return Ref<T>(static_cast<T*>(node.Get()));
	}

	// Lifetime-safe wrapper: keeps lexer, error tracker, and stream alive alongside ParserState.
	// ParserState holds references to these objects; they must outlive the state.
	// CRITICAL: member declaration order controls initialization order. State must come last.
	struct TestState {
		Ref<C4Compiler::Source> m_Stream;
		C4Compiler::Lexer m_Lexer;
		C4Compiler::ErrorTracker m_ErrorTracker;
		std::unique_ptr<C4Compiler::ParserState> m_State;

		explicit TestState(const std::string& source)
			: m_Stream(C4Compiler::Source::FromMemory(source)),
			  m_Lexer(*m_Stream, 0),
			  m_ErrorTracker(),
			  m_State(std::make_unique<C4Compiler::ParserState>(m_Lexer, m_ErrorTracker))
		{}

		// Implicit conversion so existing code passing `state` to ParserState& parameters works unchanged
		operator C4Compiler::ParserState&() { return *m_State; }
	};

	static TestState MakeState(const std::string& source) { return TestState(source); }

	// =========================================================================
	// Binary Operator Precedence
	// =========================================================================

	void PrattParserTest::TestSimpleAddition()
	{
		auto state = MakeState("a + b");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::BinaryExpression);

		auto bin = CastNode<C4Compiler::BinaryExpressionNode>(result);
		TEST_ASSERT(bin->GetOperatorToken().GetText() == "+");
		TEST_PASSED();
	}

	void PrattParserTest::TestMultiplicationBeforeAddition()
	{
		// a + b * c => Binary(+, a, Binary(*, b, c))
		auto state = MakeState("a + b * c");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::BinaryExpression);

		auto top = CastNode<C4Compiler::BinaryExpressionNode>(result);
		TEST_ASSERT(top->GetOperatorToken().GetText() == "+");

		// Left operand is identifier "a"
		TEST_ASSERT(top->GetLeftOperand()->Type() == C4Compiler::NodeType::IdentifierExpression);

		// Right operand is Binary(*, b, c)
		auto right = top->GetRightOperand();
		TEST_ASSERT(right->Type() == C4Compiler::NodeType::BinaryExpression);
		auto mul = CastNode<C4Compiler::BinaryExpressionNode>(right);
		TEST_ASSERT(mul->GetOperatorToken().GetText() == "*");
		TEST_PASSED();
	}

	void PrattParserTest::TestRightAssociativePower()
	{
		// 2 ** 3 ** 2 => Binary(**, 2, Binary(**, 3, 2)) - right associative
		auto state = MakeState("2 ** 3 ** 2");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto top = CastNode<C4Compiler::BinaryExpressionNode>(result);
		TEST_ASSERT(top->GetOperatorToken().GetText() == "**");

		// Left is literal "2"
		TEST_ASSERT(top->GetLeftOperand()->Type() == C4Compiler::NodeType::LiteralExpression);

		// Right is Binary(**, 3, 2)
		auto right = top->GetRightOperand();
		TEST_ASSERT(right->Type() == C4Compiler::NodeType::BinaryExpression);
		auto inner = CastNode<C4Compiler::BinaryExpressionNode>(right);
		TEST_ASSERT(inner->GetOperatorToken().GetText() == "**");
		TEST_PASSED();
	}

	void PrattParserTest::TestComparisonAndEqualityPrecedence()
	{
		// a < b && c == d => ConditionalAnd(Binary(<, a, b), Binary(==, c, d))
		auto state = MakeState("a < b && c == d");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::ConditionalAnd);

		auto condAnd = CastNode<C4Compiler::ConditionalAndNode>(result);

		// Left: Binary(<, a, b)
		auto left = condAnd->GetLeftOperand();
		TEST_ASSERT(left->Type() == C4Compiler::NodeType::BinaryExpression);
		auto comp = CastNode<C4Compiler::BinaryExpressionNode>(left);
		TEST_ASSERT(comp->GetOperatorToken().GetText() == "<");

		// Right: Binary(==, c, d)
		auto right = condAnd->GetRightOperand();
		TEST_ASSERT(right->Type() == C4Compiler::NodeType::BinaryExpression);
		auto eq = CastNode<C4Compiler::BinaryExpressionNode>(right);
		TEST_ASSERT(eq->GetOperatorToken().GetText() == "==");
		TEST_PASSED();
	}

	void PrattParserTest::TestBitwisePrecedence()
	{
		// a & b | c => Binary(|, Binary(&, a, b), c) - AND binds tighter than OR
		auto state = MakeState("a & b | c");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto top = CastNode<C4Compiler::BinaryExpressionNode>(result);
		TEST_ASSERT(top->GetOperatorToken().GetText() == "|");

		auto left = top->GetLeftOperand();
		TEST_ASSERT(left->Type() == C4Compiler::NodeType::BinaryExpression);
		auto bitAnd = CastNode<C4Compiler::BinaryExpressionNode>(left);
		TEST_ASSERT(bitAnd->GetOperatorToken().GetText() == "&");
		TEST_PASSED();
	}

	void PrattParserTest::TestShiftOperators()
	{
		// a << b => Binary(<<, a, b)
		auto state = MakeState("a << b");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto bin = CastNode<C4Compiler::BinaryExpressionNode>(result);
		TEST_ASSERT(bin->GetOperatorToken().GetText() == "<<");
		TEST_PASSED();
	}

	// =========================================================================
	// Unary Operators
	// =========================================================================

	void PrattParserTest::TestPrefixNegation()
	{
		auto state = MakeState("-x");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::UnaryExpression);

		auto unary = CastNode<C4Compiler::UnaryExpressionNode>(result);
		TEST_ASSERT(unary->GetOperatorToken().GetText() == "-");
		TEST_ASSERT(!unary->IsPostfix());
		TEST_PASSED();
	}

	void PrattParserTest::TestDoubleNegation()
	{
		auto state = MakeState("!!flag");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::UnaryExpression);

		auto outer = CastNode<C4Compiler::UnaryExpressionNode>(result);
		TEST_ASSERT(outer->GetOperatorToken().GetText() == "!");
		TEST_ASSERT(!outer->IsPostfix());

		auto inner = outer->GetOperand();
		TEST_ASSERT(inner->Type() == C4Compiler::NodeType::UnaryExpression);
		auto innerUnary = CastNode<C4Compiler::UnaryExpressionNode>(inner);
		TEST_ASSERT(innerUnary->GetOperatorToken().GetText() == "!");
		TEST_PASSED();
	}

	void PrattParserTest::TestPrefixLogicalNot()
	{
		auto state = MakeState("!true");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::UnaryExpression);

		auto unary = CastNode<C4Compiler::UnaryExpressionNode>(result);
		TEST_ASSERT(unary->GetOperatorToken().GetText() == "!");
		TEST_PASSED();
	}

	void PrattParserTest::TestPostfixIncrement()
	{
		auto state = MakeState("i++");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::UnaryExpression);

		auto unary = CastNode<C4Compiler::UnaryExpressionNode>(result);
		TEST_ASSERT(unary->GetOperatorToken().GetText() == "++");
		TEST_ASSERT(unary->IsPostfix());
		TEST_PASSED();
	}

	void PrattParserTest::TestPostfixDecrement()
	{
		auto state = MakeState("x--");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto unary = CastNode<C4Compiler::UnaryExpressionNode>(result);
		TEST_ASSERT(unary->GetOperatorToken().GetText() == "--");
		TEST_ASSERT(unary->IsPostfix());
		TEST_PASSED();
	}

	// =========================================================================
	// Ternary Expressions
	// =========================================================================

	void PrattParserTest::TestSimpleTernary()
	{
		auto state = MakeState("a ? b : c");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::TernaryExpression);

		auto tern = CastNode<C4Compiler::TernaryExpressionNode>(result);
		TEST_ASSERT(tern->GetCondition()->Type() == C4Compiler::NodeType::IdentifierExpression);
		TEST_ASSERT(tern->GetThenBranch()->Type() == C4Compiler::NodeType::IdentifierExpression);
		TEST_ASSERT(tern->GetElseBranch()->Type() == C4Compiler::NodeType::IdentifierExpression);
		TEST_PASSED();
	}

	void PrattParserTest::TestNestedTernaryRightAssociative()
	{
		// a ? b : c ? d : e => Ternary(a, b, Ternary(c, d, e))
		auto state = MakeState("a ? b : c ? d : e");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto outer = CastNode<C4Compiler::TernaryExpressionNode>(result);
		auto elseBranch = outer->GetElseBranch();
		TEST_ASSERT(elseBranch->Type() == C4Compiler::NodeType::TernaryExpression);
		TEST_PASSED();
	}

	// =========================================================================
	// Function Calls
	// =========================================================================

	void PrattParserTest::TestEmptyCall()
	{
		auto state = MakeState("func()");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::FunctionCall);

		auto call = CastNode<C4Compiler::FunctionCallNode>(result);
		TEST_ASSERT(call->ArgumentCount() == 0);
		TEST_PASSED();
	}

	void PrattParserTest::TestSingleArgument()
	{
		auto state = MakeState("func(x)");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto call = CastNode<C4Compiler::FunctionCallNode>(result);
		TEST_ASSERT(call->ArgumentCount() == 1);
		TEST_PASSED();
	}

	void PrattParserTest::TestMultipleArguments()
	{
		auto state = MakeState("func(a, b, c)");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto call = CastNode<C4Compiler::FunctionCallNode>(result);
		TEST_ASSERT(call->ArgumentCount() == 3);
		TEST_PASSED();
	}

	void PrattParserTest::TestExpressionAsArgument()
	{
		// func(a, b + c) - second arg is a binary expression
		auto state = MakeState("func(a, b + c)");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto call = CastNode<C4Compiler::FunctionCallNode>(result);
		TEST_ASSERT(call->ArgumentCount() == 2);

		auto secondArg = call->GetArguments()[1];
		TEST_ASSERT(secondArg->Type() == C4Compiler::NodeType::BinaryExpression);
		TEST_PASSED();
	}

	// =========================================================================
	// Member Access
	// =========================================================================

	void PrattParserTest::TestDotAccess()
	{
		auto state = MakeState("obj.field");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::MemberAccess);

		auto ma = CastNode<C4Compiler::MemberAccessNode>(result);
		TEST_ASSERT(ma->GetAccessOperatorToken().GetText() == ".");
		TEST_ASSERT(!ma->IsArrowAccess());
		TEST_PASSED();
	}

	void PrattParserTest::TestArrowAccess()
	{
		auto state = MakeState("ptr->field");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto ma = CastNode<C4Compiler::MemberAccessNode>(result);
		TEST_ASSERT(ma->GetAccessOperatorToken().GetText() == "->");
		TEST_ASSERT(ma->IsArrowAccess());
		TEST_PASSED();
	}

	void PrattParserTest::TestChainedDotAccess()
	{
		// a.b.c.d => MemberAccess(MemberAccess(MemberAccess(a, b), c), d)
		auto state = MakeState("a.b.c.d");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		// Walk the chain - should be 3 nested MemberAccess nodes
		int depth = 0;
		auto current = result;
		while (current->Type() == C4Compiler::NodeType::MemberAccess) {
			depth++;
			current = CastNode<C4Compiler::MemberAccessNode>(current)->GetBaseObject();
		}
		TEST_ASSERT(depth == 3);
		TEST_PASSED();
	}

	// =========================================================================
	// Static Access
	// =========================================================================

	void PrattParserTest::TestStaticAccess()
	{
		auto state = MakeState("A::B");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::StaticAccess);

		auto sa = CastNode<C4Compiler::StaticAccessNode>(result);
		TEST_ASSERT(sa->GetMemberNameToken().GetText() == "B");
		TEST_PASSED();
	}

	void PrattParserTest::TestChainedStaticAccess()
	{
		// A::B::C => StaticAccess(StaticAccess(A, B), C)
		auto state = MakeState("A::B::C");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		int depth = 0;
		auto current = result;
		while (current->Type() == C4Compiler::NodeType::StaticAccess) {
			depth++;
			current = CastNode<C4Compiler::StaticAccessNode>(current)->GetBaseType();
		}
		TEST_ASSERT(depth == 2);
		TEST_PASSED();
	}

	// =========================================================================
	// Array Indexing
	// =========================================================================

	void PrattParserTest::TestSimpleIndex()
	{
		auto state = MakeState("arr[0]");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::ArrayIndex);

		auto ai = CastNode<C4Compiler::ArrayIndexNode>(result);
		TEST_ASSERT(ai->GetArrayExpression()->Type() == C4Compiler::NodeType::IdentifierExpression);
		TEST_PASSED();
	}

	void PrattParserTest::TestExpressionIndex()
	{
		auto state = MakeState("arr[i + 1]");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto ai = CastNode<C4Compiler::ArrayIndexNode>(result);
		auto idx = ai->GetIndexExpression();
		TEST_ASSERT(idx->Type() == C4Compiler::NodeType::BinaryExpression);
		TEST_PASSED();
	}

	// =========================================================================
	// Postfix Chaining
	// =========================================================================

	void PrattParserTest::TestCallThenIndex()
	{
		// func()[0] => ArrayIndex(FunctionCall(func), 0)
		auto state = MakeState("func()[0]");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto ai = CastNode<C4Compiler::ArrayIndexNode>(result);
		auto arrExpr = ai->GetArrayExpression();
		TEST_ASSERT(arrExpr->Type() == C4Compiler::NodeType::FunctionCall);
		TEST_PASSED();
	}

	void PrattParserTest::TestMemberThenCall()
	{
		// obj.Method() => FunctionCall(MemberAccess(obj, Method))
		auto state = MakeState("obj.Method()");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto call = CastNode<C4Compiler::FunctionCallNode>(result);
		auto callee = call->GetCallee();
		TEST_ASSERT(callee->Type() == C4Compiler::NodeType::MemberAccess);
		TEST_PASSED();
	}

	void PrattParserTest::TestComplexPostfixChain()
	{
		// func(a, b ? c : d)[i++]->field
		auto state = MakeState("func(a, b ? c : d)[i++]->field");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		// Top level: MemberAccess(..., field) with arrow
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::MemberAccess);
		auto ma = CastNode<C4Compiler::MemberAccessNode>(result);
		TEST_ASSERT(ma->GetMemberNameToken().GetText() == "field");
		TEST_ASSERT(ma->IsArrowAccess());

		// Base: ArrayIndex on FunctionCall
		auto base = ma->GetBaseObject();
		TEST_ASSERT(base->Type() == C4Compiler::NodeType::ArrayIndex);
		auto ai = CastNode<C4Compiler::ArrayIndexNode>(base);

		auto arrExpr = ai->GetArrayExpression();
		TEST_ASSERT(arrExpr->Type() == C4Compiler::NodeType::FunctionCall);
		auto call = CastNode<C4Compiler::FunctionCallNode>(arrExpr);
		TEST_ASSERT(call->ArgumentCount() == 2);

		// Second arg is ternary
		auto secondArg = call->GetArguments()[1];
		TEST_ASSERT(secondArg->Type() == C4Compiler::NodeType::TernaryExpression);

		// Index expression is postfix increment
		auto idxExpr = ai->GetIndexExpression();
		TEST_ASSERT(idxExpr->Type() == C4Compiler::NodeType::UnaryExpression);
		auto postfix = CastNode<C4Compiler::UnaryExpressionNode>(idxExpr);
		TEST_ASSERT(postfix->IsPostfix());
		TEST_PASSED();
	}

	// =========================================================================
	// Parenthesized Expressions
	// =========================================================================

	void PrattParserTest::TestSimpleParenthesized()
	{
		auto state = MakeState("(a + b)");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::ParenthesizedExpression);

		auto pe = CastNode<C4Compiler::ParenthesizedExpressionNode>(result);
		auto inner = pe->GetExpression();
		TEST_ASSERT(inner->Type() == C4Compiler::NodeType::BinaryExpression);
		TEST_PASSED();
	}

	void PrattParserTest::TestNestedParentheses()
	{
		auto state = MakeState("((a))");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		int depth = 0;
		auto current = result;
		while (current->Type() == C4Compiler::NodeType::ParenthesizedExpression) {
			depth++;
			current = CastNode<C4Compiler::ParenthesizedExpressionNode>(current)->GetExpression();
		}
		TEST_ASSERT(depth == 2);
		TEST_PASSED();
	}

	// =========================================================================
	// Conditional And/Or
	// =========================================================================

	void PrattParserTest::TestConditionalAnd()
	{
		auto state = MakeState("a && b");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::ConditionalAnd);
		TEST_PASSED();
	}

	void PrattParserTest::TestConditionalOr()
	{
		auto state = MakeState("a || b");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::ConditionalOr);
		TEST_PASSED();
	}

	void PrattParserTest::TestAndHigherThanOr()
	{
		// a || b && c => ConditionalOr(a, ConditionalAnd(b, c))
		auto state = MakeState("a || b && c");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto top = CastNode<C4Compiler::ConditionalOrNode>(result);
		auto right = top->GetRightOperand();
		TEST_ASSERT(right->Type() == C4Compiler::NodeType::ConditionalAnd);
		TEST_PASSED();
	}

	// =========================================================================
	// Assignments
	// =========================================================================

	void PrattParserTest::TestSimpleAssignment()
	{
		auto state = MakeState("x = a + b * c");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::AssignmentExpression);

		auto assign = CastNode<C4Compiler::AssignmentExpressionNode>(result);
		auto right = assign->GetRightOperand();
		// Right side: a + (b * c) - proper precedence
		TEST_ASSERT(right->Type() == C4Compiler::NodeType::BinaryExpression);
		auto bin = CastNode<C4Compiler::BinaryExpressionNode>(right);
		TEST_ASSERT(bin->GetOperatorToken().GetText() == "+");
		TEST_PASSED();
	}

	void PrattParserTest::TestCompoundAssignment()
	{
		auto state = MakeState("x += 5");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto assign = CastNode<C4Compiler::AssignmentExpressionNode>(result);
		TEST_ASSERT(assign->GetOperatorToken().GetText() == "+=");
		TEST_PASSED();
	}

	// =========================================================================
	// Casts
	// =========================================================================

	void PrattParserTest::TestCStyleCast()
	{
		auto state = MakeState("(float)value");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::CastExpression);

		auto cast = CastNode<C4Compiler::CastExpressionNode>(result);
		TEST_ASSERT(cast->GetKind() == C4Compiler::CastExpressionNode::CastKind::CStyle);
		TEST_PASSED();
	}

	void PrattParserTest::TestStaticCast()
	{
		auto state = MakeState("static_cast<int>(x)");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::CastExpression);

		auto cast = CastNode<C4Compiler::CastExpressionNode>(result);
		TEST_ASSERT(cast->GetKind() == C4Compiler::CastExpressionNode::CastKind::StaticCast);
		TEST_PASSED();
	}

	void PrattParserTest::TestCStyleCastBindsTighterThanAddition()
	{
		// (int)f + 1 must be Binary(+, Cast(int, f), 1) - the cast wraps ONLY `f`.
		auto state = MakeState("(int)f + 1");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT_MSG(result->Type() == C4Compiler::NodeType::BinaryExpression, "top node must be the + binary op, got {0}", (int)result->Type());

		auto bin = CastNode<C4Compiler::BinaryExpressionNode>(result);
		TEST_ASSERT(bin->GetOperatorToken().GetText() == "+");
		TEST_ASSERT_MSG(bin->GetLeftOperand()->Type() == C4Compiler::NodeType::CastExpression, "left of + must be the cast, got {0}", (int)bin->GetLeftOperand()->Type());
		auto cast = CastNode<C4Compiler::CastExpressionNode>(bin->GetLeftOperand());
		TEST_ASSERT_MSG(cast->GetKind() == C4Compiler::CastExpressionNode::CastKind::CStyle, "cast kind must be CStyle");
		TEST_PASSED();
	}

	void PrattParserTest::TestCStyleCastBindsTighterThanMultiplication()
	{
		// (int)f * 2 must be Binary(*, Cast(int, f), 2).
		auto state = MakeState("(int)f * 2");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT_MSG(result->Type() == C4Compiler::NodeType::BinaryExpression, "top node must be the * binary op, got {0}", (int)result->Type());

		auto bin = CastNode<C4Compiler::BinaryExpressionNode>(result);
		TEST_ASSERT(bin->GetOperatorToken().GetText() == "*");
		TEST_ASSERT_MSG(bin->GetLeftOperand()->Type() == C4Compiler::NodeType::CastExpression, "left of * must be the cast, got {0}", (int)bin->GetLeftOperand()->Type());
		TEST_PASSED();
	}

	void PrattParserTest::TestCStyleCastBindsTighterThanComparison()
	{
		// (int)f == 1 must be Binary(==, Cast(int, f), 1). This was the original symptom:
		// it parsed as Cast(int, Binary(==, f, 1)) and evaluated to the wrong value.
		auto state = MakeState("(int)f == 1");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT_MSG(result->Type() == C4Compiler::NodeType::BinaryExpression, "top node must be the == binary op, got {0}", (int)result->Type());

		auto bin = CastNode<C4Compiler::BinaryExpressionNode>(result);
		TEST_ASSERT(bin->GetOperatorToken().GetText() == "==");
		TEST_ASSERT_MSG(bin->GetLeftOperand()->Type() == C4Compiler::NodeType::CastExpression, "left of == must be the cast, got {0}", (int)bin->GetLeftOperand()->Type());
		TEST_PASSED();
	}

	void PrattParserTest::TestCStyleCastOfParenthesizedOperandKeepsWholeExpr()
	{
		// (int)(f + 1) must still wrap the WHOLE parenthesized expression: Cast(int, Paren(f + 1)).
		auto state = MakeState("(int)(f + 1)");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT_MSG(result->Type() == C4Compiler::NodeType::CastExpression, "top node must be the cast, got {0}", (int)result->Type());

		auto cast = CastNode<C4Compiler::CastExpressionNode>(result);
		TEST_ASSERT_MSG(cast->GetOperand()->Type() == C4Compiler::NodeType::ParenthesizedExpression, "cast operand must be the parenthesized expr, got {0}", (int)cast->GetOperand()->Type());
		TEST_PASSED();
	}

	// =========================================================================
	// Integration: ExpressionStatementNode
	// =========================================================================

	void PrattParserTest::TestExpressionAsStatement()
	{
		auto state = MakeState("x = a + b * c;");
		auto result = C4Compiler::ExpressionStatementNode::TryParse(state);
		TEST_ASSERT(result != nullptr);

		auto expr = result->GetExpression();
		TEST_ASSERT(expr->Type() == C4Compiler::NodeType::AssignmentExpression);

		auto assign = CastNode<C4Compiler::AssignmentExpressionNode>(expr);
		auto right = assign->GetRightOperand();
		TEST_ASSERT(right->Type() == C4Compiler::NodeType::BinaryExpression);
		TEST_PASSED();
	}

	void PrattParserTest::TestComplexExpressionAsStatement()
	{
		auto state = MakeState("func(a, b ? c : d)[i++]->field;");
		auto result = C4Compiler::ExpressionStatementNode::TryParse(state);
		TEST_ASSERT(result != nullptr);

		auto expr = result->GetExpression();
		TEST_ASSERT(expr->Type() == C4Compiler::NodeType::MemberAccess);
		TEST_PASSED();
	}

	// =========================================================================
	// Integration: CodeBlockNode
	// =========================================================================

	void PrattParserTest::TestCodeBlockWithBinaryExpressions()
	{
		auto state = MakeState("{ a + b; x * y + z; }");
		auto result = C4Compiler::CodeBlockNode::TryParse(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->StatementCount() == 2);

		// First statement: a + b
		auto stmt1 = CastNode<C4Compiler::ExpressionStatementNode>(result->GetStatements()[0]);
		TEST_ASSERT(stmt1->GetExpression()->Type() == C4Compiler::NodeType::BinaryExpression);

		// Second statement: x * y + z => Binary(+, Binary(*, x, y), z)
		auto stmt2 = CastNode<C4Compiler::ExpressionStatementNode>(result->GetStatements()[1]);
		auto expr2 = stmt2->GetExpression();
		TEST_ASSERT(expr2->Type() == C4Compiler::NodeType::BinaryExpression);
		auto topBin = CastNode<C4Compiler::BinaryExpressionNode>(expr2);
		TEST_ASSERT(topBin->GetOperatorToken().GetText() == "+");

		auto rightOp = topBin->GetLeftOperand();
		TEST_ASSERT(rightOp->Type() == C4Compiler::NodeType::BinaryExpression);
		auto mul = CastNode<C4Compiler::BinaryExpressionNode>(rightOp);
		TEST_ASSERT(mul->GetOperatorToken().GetText() == "*");
		TEST_PASSED();
	}

	void PrattParserTest::TestCodeBlockWithFunctionCalls()
	{
		auto state = MakeState("{ obj.Method(a, b + c); }");
		auto result = C4Compiler::CodeBlockNode::TryParse(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(result->StatementCount() == 1);

		auto stmt = CastNode<C4Compiler::ExpressionStatementNode>(result->GetStatements()[0]);
		auto expr = stmt->GetExpression();
		TEST_ASSERT(expr->Type() == C4Compiler::NodeType::FunctionCall);

		auto call = CastNode<C4Compiler::FunctionCallNode>(expr);
		TEST_ASSERT(call->ArgumentCount() == 2);
		TEST_PASSED();
	}

	// =========================================================================
	// Template Argument Disambiguation (< vs Less)
	// =========================================================================

	void PrattParserTest::TestStaticAccessThenTemplate()
	{
		// Vector<float> - this parses as StaticAccess(Vector, float) via :: handling
		// The < in template context is handled by postfix chain on identifiers
		// For now, test that a < b is parsed as comparison (not confused with template)
		auto state = MakeState("a < b");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		auto bin = CastNode<C4Compiler::BinaryExpressionNode>(result);
		TEST_ASSERT(bin->GetOperatorToken().GetText() == "<");
		TEST_PASSED();
	}

	void PrattParserTest::TestComparisonNotConfusedWithTemplate()
	{
		// a < b > c - both should be Binary comparison operators
		auto state = MakeState("a < b > c");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);

		// Top is Binary(>, Binary(<, a, b), c) since < and > have same precedence, left-associative
		// Actually: both at BP 40, left-assoc. So: Binary(<, a, b) then > c => Binary(>, Binary(<, a, b), c)
		auto top = CastNode<C4Compiler::BinaryExpressionNode>(result);
		TEST_ASSERT(top->GetOperatorToken().GetText() == ">");

		auto left = top->GetLeftOperand();
		TEST_ASSERT(left->Type() == C4Compiler::NodeType::BinaryExpression);
		auto inner = CastNode<C4Compiler::BinaryExpressionNode>(left);
		TEST_ASSERT(inner->GetOperatorToken().GetText() == "<");
		TEST_PASSED();
	}

	void PrattParserTest::TestComparisonWithCallOnTheRight()
	{
		// BUG-083: `i < Make(x).v` is a comparison; the speculative template-argument parse fails at '('
		// and must roll back instead of committing a broken template call.
		auto state = MakeState("i < Make(x).v");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(state.m_ErrorTracker.GetErrors().Empty());
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::BinaryExpression);
		auto bin = CastNode<C4Compiler::BinaryExpressionNode>(result);
		TEST_ASSERT(bin->GetOperatorToken().GetText() == "<");
		TEST_ASSERT(bin->GetRightOperand()->Type() == C4Compiler::NodeType::MemberAccess);
		TEST_PASSED();
	}

	void PrattParserTest::TestExplicitTemplateCall()
	{
		// A complete argument list followed by '(' is still an explicit template call.
		auto state = MakeState("Make<int32>(x)");
		auto result = C4Compiler::PrattParser::ParseExpression(state);
		TEST_ASSERT(result != nullptr);
		TEST_ASSERT(state.m_ErrorTracker.GetErrors().Empty());
		TEST_ASSERT(result->Type() == C4Compiler::NodeType::FunctionCall);
		auto call = CastNode<C4Compiler::FunctionCallNode>(result);
		TEST_ASSERT(call->GetTemplateArgs() != nullptr);
		TEST_ASSERT(call->ArgumentCount() == 1);
		TEST_PASSED();
	}

	void PrattParserTest::TestDeepNestingIsRejected()
	{
		// Parsing is recursive descent, so unbounded nesting would overflow the compiler's stack and
		// crash it. The depth cap must turn that into a clean error instead of a crash. Note the parser
		// uses error recovery (it may still return a partial node), so the contract is: a "nesting too
		// deep" error is reported and the depth counter fully unwinds afterwards.
		std::string deep;
		for (int i = 0; i < C4Compiler::ParserState::kMaxParseDepth + 100; ++i) deep += "(";
		deep += "1";
		for (int i = 0; i < C4Compiler::ParserState::kMaxParseDepth + 100; ++i) deep += ")";

		auto state = MakeState(deep);
		C4Compiler::PrattParser::ParseExpression(state);

		bool foundDepthError = false;
		for (const auto& e : state.m_ErrorTracker.GetErrors()) {
			if (e.message.find("nesting too deep") != Core::String::npos) foundDepthError = true;
		}
		TEST_ASSERT_MSG(foundDepthError, "a 'nesting too deep' error must be reported");

		// The depth counter must have fully unwound: a subsequent normal parse still works.
		auto state2 = MakeState("a + b");
		auto ok = C4Compiler::PrattParser::ParseExpression(state2);
		TEST_ASSERT(ok != nullptr);
		TEST_PASSED();
	}

	void PrattParserTest::TestDeepBlockNestingIsRejected()
	{
		// Blocks are a separate recursive-descent path from expressions (CodeBlockNode::TryParse ->
		// ParseBody -> nested statement -> nested block), so the expression-depth cap does not cover
		// them. Without the block cap, thousands of nested `{ }` blocks overflowed the stack and
		// crashed the compiler. It must now be a clean error instead.
		std::string deep;
		for (int i = 0; i < C4Compiler::ParserState::kMaxBlockDepth + 100; ++i) deep += "{";
		deep += ";";
		for (int i = 0; i < C4Compiler::ParserState::kMaxBlockDepth + 100; ++i) deep += "}";

		auto state = MakeState(deep);
		// Drive the exact guarded path: nested code blocks.
		C4Compiler::CodeBlockNode::TryParse(state);

		bool foundBlockError = false;
		for (const auto& e : state.m_ErrorTracker.GetErrors()) {
			if (e.message.find("Block nesting too deep") != Core::String::npos) foundBlockError = true;
		}
		TEST_ASSERT_MSG(foundBlockError, "a 'Block nesting too deep' error must be reported");

		// The block-depth counter must have fully unwound: a subsequent normal block still parses.
		auto state2 = MakeState("{ a + b; }");
		auto ok = C4Compiler::CodeBlockNode::TryParse(state2);
		TEST_ASSERT(ok != nullptr);
		TEST_PASSED();
	}
}
