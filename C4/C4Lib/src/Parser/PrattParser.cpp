#include "Parser/PrattParser.h"
#include "Lexer/Lexer.h"
#include "Core/StringFormat.h"
#include "Nodes/Expressions/BinaryExpressionNode.h"
#include "Nodes/Expressions/UnaryExpressionNode.h"
#include "Nodes/Expressions/PackExpansionNode.h"
#include "Nodes/Expressions/TernaryExpressionNode.h"
#include "Nodes/Expressions/FunctionCallNode.h"
#include "Nodes/Expressions/MemberAccessNode.h"
#include "Nodes/Expressions/StaticAccessNode.h"
#include "Nodes/Expressions/ValueListNode.h"
#include "Nodes/Expressions/ArrayIndexNode.h"
#include "Nodes/Expressions/AssignmentExpressionNode.h"
#include "Nodes/Expressions/ConditionalAndNode.h"
#include "Nodes/Expressions/ConditionalOrNode.h"
#include "Nodes/Expressions/TypeCheckNode.h"
#include "Nodes/Expressions/SafeCastNode.h"
#include "Nodes/Expressions/CastExpressionNode.h"
#include "Nodes/Expressions/ParenthesizedExpressionNode.h"
#include "Nodes/Expressions/BracedInitializerNode.h"

namespace Scripting
{
	namespace
	{
		// The binary operators, C++'s levels (D-27, docs/language/10 1): higher binds tighter.
		struct InfixOperator { TokenType token; uint32 power; bool rightAssociative; };
		constexpr InfixOperator kInfixOperators[] = {
			{ TokenType::Equal, BindPower::Assignment, true },
			{ TokenType::PlusEquals, BindPower::Assignment, true },
			{ TokenType::MinusEquals, BindPower::Assignment, true },
			{ TokenType::StarEquals, BindPower::Assignment, true },
			{ TokenType::SlashEquals, BindPower::Assignment, true },
			{ TokenType::PercentEquals, BindPower::Assignment, true },
			{ TokenType::AmpersandEquals, BindPower::Assignment, true },
			{ TokenType::PipeEquals, BindPower::Assignment, true },
			{ TokenType::CaretEquals, BindPower::Assignment, true },
			{ TokenType::LessLessEquals, BindPower::Assignment, true },
			{ TokenType::GreaterGreaterEquals, BindPower::Assignment, true },
			{ TokenType::QuestionMark, BindPower::Assignment, true },
			{ TokenType::DoublePipe, BindPower::LogicalOr, false },
			{ TokenType::DoubleAmpersand, BindPower::LogicalAnd, false },
			{ TokenType::Pipe, BindPower::BitOr, false },
			{ TokenType::Caret, BindPower::BitXor, false },
			{ TokenType::Ampersand, BindPower::BitAnd, false },
			{ TokenType::DoubleEqual, BindPower::Equality, false },
			{ TokenType::ExclamationEquals, BindPower::Equality, false },
			{ TokenType::Less, BindPower::Relational, false },
			{ TokenType::Greater, BindPower::Relational, false },
			{ TokenType::LessOrEqual, BindPower::Relational, false },
			{ TokenType::GreaterOrEqual, BindPower::Relational, false },
			{ TokenType::Is, BindPower::Relational, false },
			{ TokenType::As, BindPower::Relational, false },
			{ TokenType::DoubleLess, BindPower::Shift, false },
			{ TokenType::DoubleGreater, BindPower::Shift, false },
			{ TokenType::Plus, BindPower::Additive, false },
			{ TokenType::Minus, BindPower::Additive, false },
			{ TokenType::Star, BindPower::Multiplicative, false },
			{ TokenType::ForwardSlash, BindPower::Multiplicative, false },
			{ TokenType::Percent, BindPower::Multiplicative, false },
			{ TokenType::DoubleStar, BindPower::Power, true },
		};

		const InfixOperator* FindInfix(TokenType tokenType)
		{
			for (const InfixOperator& op : kInfixOperators)
				if (op.token == tokenType) return &op;
			return nullptr;
		}
	}

	uint32 PrattParser::InfixBindPower(TokenType tokenType)
	{
		const InfixOperator* op = FindInfix(tokenType);
		return op ? op->power : 0;
	}

	bool PrattParser::IsRightAssociative(TokenType tokenType)
	{
		const InfixOperator* op = FindInfix(tokenType);
		return op && op->rightAssociative;
	}

	// =========================================================================
	// Main entry point
	// =========================================================================

	Ref<AstNode> PrattParser::ParseArgument(ParserState& state)
	{
		return ExpandIfPack(state, ParseExpression(state, 0));
	}

	Ref<AstNode> PrattParser::ExpandIfPack(ParserState& state, const Ref<AstNode>& parsed)
	{
		Token next = state.GetLexer().PeekCompound();
		if (!parsed || next.GetType() != TokenType::Ellipsis) return parsed;
		state.GetLexer().NextCompoundToken();
		return PackExpansionNode::Around(parsed, next);
	}

	Ref<AstNode> PrattParser::ParseDesignator(ParserState& state)
	{
		return ParseExpression(state, InfixBindPower(TokenType::Equal) + 1);
	}

	Ref<AstNode> PrattParser::ParseTemplateArgumentExpression(ParserState& state)
	{
		return ParseExpression(state, InfixBindPower(TokenType::Greater) + 1);
	}

	Ref<AstNode> PrattParser::ParseExpression(ParserState& state, uint32 minBindPower)
	{
		// Guard against stack overflow: parsing is recursive descent, so unbounded nesting of
		// expressions (e.g. thousands of parens or calls) would crash the compiler. Cap it and
		// report a clean error instead.
		if (state.GetParseDepth() >= ParserState::kMaxParseDepth) {
			Token t = state.GetLexer().PeekCompound();
			state.GetErrorTracker().AddError(
				t.GetStartLocation(),
				Core::StringFormat::Format("Expression nesting too deep (maximum is {0}); add statements or parentheses to break up the expression", ParserState::kMaxParseDepth),
				ErrorSeverity::Error);
			return nullptr;
		}

		state.EnterParseLevel();
		struct ParseDepthGuard {
			ParserState& m_State;
			~ParseDepthGuard() { m_State.LeaveParseLevel(); }
		};
		ParseDepthGuard depthGuard{ state };

		// 1. Parse prefix (primary) expression
		auto left = ParsePrefix(state);
		if (!left) return nullptr;

		// 2. Handle postfix operators on the primary
		left = ParsePostfix(state, left);

		// 3. Loop: handle infix operators with higher or equal bind power
		while (true)
		{
			Token next = state.GetLexer().PeekCompound();
			uint32 bp = InfixBindPower(next.GetType());
			if (bp == 0 || bp < minBindPower) break;

			bool rightAssoc = IsRightAssociative(next.GetType());
			uint32 nextBP = rightAssoc ? bp : bp + 1;

			left = ParseInfix(state, left, nextBP);
			if (!left) break;

			// Handle postfix on the result of infix (e.g. a+b->member)
			left = ParsePostfix(state, left);
		}

		return left;
	}

	// =========================================================================
	// Prefix parsing - primary expressions and unary prefix operators
	// =========================================================================
} // namespace Scripting
