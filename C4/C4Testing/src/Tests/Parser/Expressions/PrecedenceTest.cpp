#include "PrecedenceTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/PrattParser.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Expressions/AssignmentExpressionNode.h"
#include "Parser/Nodes/Expressions/BinaryExpressionNode.h"
#include "Parser/Nodes/Expressions/CastExpressionNode.h"
#include "Parser/Nodes/Expressions/ConditionalAndNode.h"
#include "Parser/Nodes/Expressions/ConditionalOrNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/SafeCastNode.h"
#include "Parser/Nodes/Expressions/TernaryExpressionNode.h"
#include "Parser/Nodes/Expressions/TypeCheckNode.h"
#include "Parser/Nodes/Expressions/UnaryExpressionNode.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		std::string Text(const Core::String& s) { return std::string(s.c_str()); }

		// The tree with every operation parenthesized: `a + b * c` is "(a+(b*c))".
		std::string Shape(const AstNode* node)
		{
			if (!node) return "?";
			if (const auto* b = dynamic_cast<const BinaryExpressionNode*>(node))
				return "(" + Shape(b->GetLeftOperand().Get()) + Text(b->GetOperatorToken().GetText()) + Shape(b->GetRightOperand().Get()) + ")";
			if (const auto* a = dynamic_cast<const ConditionalAndNode*>(node))
				return "(" + Shape(a->GetLeftOperand().Get()) + "&&" + Shape(a->GetRightOperand().Get()) + ")";
			if (const auto* o = dynamic_cast<const ConditionalOrNode*>(node))
				return "(" + Shape(o->GetLeftOperand().Get()) + "||" + Shape(o->GetRightOperand().Get()) + ")";
			if (const auto* s = dynamic_cast<const AssignmentExpressionNode*>(node))
				return "(" + Shape(s->GetLeftOperand().Get()) + Text(s->GetOperatorToken().GetText()) + Shape(s->GetRightOperand().Get()) + ")";
			if (const auto* t = dynamic_cast<const TernaryExpressionNode*>(node))
				return "(" + Shape(t->GetCondition().Get()) + "?" + Shape(t->GetThenBranch().Get()) + ":" + Shape(t->GetElseBranch().Get()) + ")";
			if (const auto* u = dynamic_cast<const UnaryExpressionNode*>(node))
				return "(" + Text(u->GetOperatorToken().GetText()) + Shape(u->GetOperand().Get()) + ")";
			if (const auto* is = dynamic_cast<const TypeCheckNode*>(node))
				return "(" + Shape(is->GetOperand().Get()) + " is " + Text(is->GetTargetType()->ToString()) + ")";
			if (const auto* c = dynamic_cast<const CastExpressionNode*>(node))
				return "((" + Text(c->GetTargetType()->ToString()) + ")" + Shape(c->GetOperand().Get()) + ")";
			if (const auto* as = dynamic_cast<const SafeCastNode*>(node))
				return "(" + Shape(as->GetOperand().Get()) + " as " + Text(as->GetTargetType()->ToString()) + ")";
			return Text(node->ToString());
		}

		std::string Parse(const std::string& source)
		{
			Ref<Source> stream = Source::FromMemory(source);
			Lexer lexer(*stream, 0);
			ErrorTracker errors;
			ParserState state(lexer, errors);
			auto node = PrattParser::ParseExpression(state);
			return errors.GetErrors().Empty() ? Shape(node.Get()) : "error";
		}
	}

	void PrecedenceTest::AdjacentLevelsBothOrders()
	{
		// Lowest first: each operator binds looser than the next one.
		const std::vector<std::string> levels = { "||", "&&", "|", "^", "&", "==", "<", "<<", "+", "*", "**" };
		for (size_t i = 0; i + 1 < levels.size(); ++i)
		{
			const std::string& low = levels[i];
			const std::string& high = levels[i + 1];
			TEST_ASSERT_EQ(Parse("a " + low + " b " + high + " c"), "(a" + low + "(b" + high + "c))");
			TEST_ASSERT_EQ(Parse("a " + high + " b " + low + " c"), "((a" + high + "b)" + low + "c)");
		}
		// Each level's other members share it.
		TEST_ASSERT_EQ(Parse("a != b >= c"), std::string("(a!=(b>=c))"));
		TEST_ASSERT_EQ(Parse("a >> b - c"), std::string("(a>>(b-c))"));
		TEST_ASSERT_EQ(Parse("a - b % c"), std::string("(a-(b%c))"));
	}

	void PrecedenceTest::TypeOperatorsAreRelational()
	{
		TEST_ASSERT_EQ(Parse("x is Node == ok"), std::string("((x is Node)==ok)"));
		TEST_ASSERT_EQ(Parse("p as Node* == q"), std::string("((p as Node*)==q)"));
		TEST_ASSERT_EQ(Parse("a && x is Node"), std::string("(a&&(x is Node))"));
	}

	void PrecedenceTest::RightAssociativeOperators()
	{
		TEST_ASSERT_EQ(Parse("a = b = c"), std::string("(a=(b=c))"));
		TEST_ASSERT_EQ(Parse("a += b -= c"), std::string("(a+=(b-=c))"));
		TEST_ASSERT_EQ(Parse("a ? b : c ? d : e"), std::string("(a?b:(c?d:e))"));
		TEST_ASSERT_EQ(Parse("a ** b ** c"), std::string("(a**(b**c))"));
	}

	void PrecedenceTest::LeftAssociativeOperators()
	{
		TEST_ASSERT_EQ(Parse("a - b - c"), std::string("((a-b)-c)"));
		TEST_ASSERT_EQ(Parse("a / b / c"), std::string("((a/b)/c)"));
		TEST_ASSERT_EQ(Parse("a << b << c"), std::string("((a<<b)<<c)"));
		TEST_ASSERT_EQ(Parse("a < b < c"), std::string("((a<b)<c)"));
		TEST_ASSERT_EQ(Parse("a && b && c"), std::string("((a&&b)&&c)"));
	}

	void PrecedenceTest::PowerAgainstPrefixOperators()
	{
		TEST_ASSERT_EQ(Parse("-x ** 2"), std::string("(-(x**2))"));
		TEST_ASSERT_EQ(Parse("2 ** -1"), std::string("(2**(-1))"));
		TEST_ASSERT_EQ(Parse("-x * 2"), std::string("((-x)*2)"));
		TEST_ASSERT_EQ(Parse("a * b ** c"), std::string("(a*(b**c))"));
		TEST_ASSERT_EQ(Parse("(float)x ** 2"), std::string("((float)(x**2))"));
		TEST_ASSERT_EQ(Parse("(float)x * 2"), std::string("(((float)x)*2)"));
	}

	void PrecedenceTest::AssignmentTakesTheWholeRightSide()
	{
		TEST_ASSERT_EQ(Parse("x = a && b"), std::string("(x=(a&&b))"));
		TEST_ASSERT_EQ(Parse("x = a || b"), std::string("(x=(a||b))"));
		TEST_ASSERT_EQ(Parse("x = c ? 1 : 2"), std::string("(x=(c?1:2))"));
		TEST_ASSERT_EQ(Parse("x = p as Node*"), std::string("(x=(p as Node*))"));
		TEST_ASSERT_EQ(Parse("a & 1 == 0"), std::string("(a&(1==0))"));
		TEST_ASSERT_EQ(Parse("a < b == c"), std::string("((a<b)==c)"));
	}

	void PrecedenceTest::ConditionalBranches()
	{
		// The middle operand is a whole expression; the last one is an assignment expression (C++).
		TEST_ASSERT_EQ(Parse("c ? x = 1 : y"), std::string("(c?(x=1):y)"));
		TEST_ASSERT_EQ(Parse("c ? a : b = 2"), std::string("(c?a:(b=2))"));
		TEST_ASSERT_EQ(Parse("a || b ? c : d"), std::string("((a||b)?c:d)"));
	}
} // namespace Testing
