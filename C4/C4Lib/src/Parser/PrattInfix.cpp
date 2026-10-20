#include "Parser/PrattParser.h"
#include "Lexer/Lexer.h"
#include "Core/StringFormat.h"
#include "Nodes/Expressions/BinaryExpressionNode.h"
#include "Nodes/Expressions/UnaryExpressionNode.h"
#include "Nodes/Expressions/TernaryExpressionNode.h"
#include "Nodes/Expressions/FunctionCallNode.h"
#include "Nodes/Expressions/MemberAccessNode.h"
#include "Nodes/Expressions/StaticAccessNode.h"
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
	Ref<AstNode> PrattParser::ParseInfix(ParserState& state, Ref<AstNode> left, uint32 leftBindPower)
	{
		Lexer& lexer = state.GetLexer();
		Token opToken = lexer.PeekCompound();
		TokenType opType = opToken.GetType();

		// --- Ternary: condition ? thenExpr : elseExpr ---
		if (opType == TokenType::QuestionMark)
		{
			auto node = CreateRef<TernaryExpressionNode>();
			node->m_Condition = left;
			node->m_QuestionToken = opToken;
			node->startLocation = left->startLocation;
			lexer.NextCompoundToken(); // consume ?

			auto thenBranch = ParseExpression(state, 0);
			if (!thenBranch)
			{
				state.GetErrorTracker().AddError(
					opToken.GetStartLocation(),
					"Expected expression after '?'", ErrorSeverity::Error);
				node->m_ThenBranch = nullptr;
			}
			else
				node->m_ThenBranch = thenBranch;

			Token colon = lexer.PeekCompound();
			if (colon.GetType() != TokenType::Colon)
			{
				state.GetErrorTracker().AddError(
					colon.GetStartLocation(),
					Core::StringFormat::Format("Expected ':' in ternary expression, got '{0}'", colon.GetText()),
					ErrorSeverity::Error);
				node->m_ColonToken = colon;
				if (node->m_ThenBranch)
					node->endLocation = node->m_ThenBranch->endLocation;
				return node;
			}
			node->m_ColonToken = colon;
			lexer.NextCompoundToken(); // consume :

			auto elseBranch = ParseExpression(state, leftBindPower);
			if (!elseBranch)
			{
				state.GetErrorTracker().AddError(
					colon.GetEndLocation(),
					"Expected expression after ':' in ternary", ErrorSeverity::Error);
			}
			else
			{
				node->m_ElseBranch = elseBranch;
				node->endLocation = elseBranch->endLocation;
			}

			return node;
		}

		// --- `operand is Type` ---
		if (opType == TokenType::Is)
		{
			auto node = CreateRef<TypeCheckNode>();
			node->m_Operand = left;
			node->m_IsToken = opToken;
			node->startLocation = left->startLocation;
			node->endLocation = opToken.GetEndLocation();
			lexer.NextCompoundToken(); // consume 'is'
			node->m_TargetType = ParseTargetType(state, opToken);
			if (node->m_TargetType)
				node->endLocation = node->m_TargetType->endLocation;
			return node;
		}

		// --- `operand as Type` ---
		if (opType == TokenType::As)
		{
			auto node = CreateRef<SafeCastNode>();
			node->m_Operand = left;
			node->m_AsToken = opToken;
			node->startLocation = left->startLocation;
			node->endLocation = opToken.GetEndLocation();
			lexer.NextCompoundToken(); // consume 'as'
			node->m_TargetType = ParseTargetType(state, opToken);
			if (node->m_TargetType)
				node->endLocation = node->m_TargetType->endLocation;
			return node;
		}

		// --- Assignment operators: =, +=, -=, *=, /=, %=, &=, |=, ^=, <<=, >>= ---
		if (opType == TokenType::Equal || opType == TokenType::PlusEquals ||
			opType == TokenType::MinusEquals || opType == TokenType::StarEquals ||
			opType == TokenType::SlashEquals || opType == TokenType::PercentEquals ||
			opType == TokenType::AmpersandEquals || opType == TokenType::PipeEquals ||
			opType == TokenType::CaretEquals || opType == TokenType::LessLessEquals ||
			opType == TokenType::GreaterGreaterEquals)
		{
			auto node = CreateRef<AssignmentExpressionNode>();
			node->m_LeftOperand = left;
			node->m_OperatorToken = opToken;
			node->startLocation = left->startLocation;
			lexer.NextCompoundToken(); // consume operator

			auto right = ParseExpression(state, leftBindPower);
			if (!right)
			{
				state.GetErrorTracker().AddError(
					opToken.GetStartLocation(),
					Core::StringFormat::Format("Expected expression after '{0}'", opToken.GetText()),
					ErrorSeverity::Error);
			}
			else
			{
				node->m_RightOperand = right;
				node->endLocation = right->endLocation;
			}
			return node;
		}

		// --- Logical OR (||) - special node type ---
		if (opType == TokenType::DoublePipe)
		{
			auto node = CreateRef<ConditionalOrNode>();
			node->m_LeftOperand = left;
			node->m_OperatorToken = opToken;
			node->startLocation = left->startLocation;
			lexer.NextCompoundToken();

			auto right = ParseExpression(state, leftBindPower);
			if (!right)
			{
				state.GetErrorTracker().AddError(
					opToken.GetStartLocation(), "Expected expression after '||'", ErrorSeverity::Error);
			}
			else
			{
				node->m_RightOperand = right;
				node->endLocation = right->endLocation;
			}
			return node;
		}

		// --- Logical AND (&&) - special node type ---
		if (opType == TokenType::DoubleAmpersand)
		{
			auto node = CreateRef<ConditionalAndNode>();
			node->m_LeftOperand = left;
			node->m_OperatorToken = opToken;
			node->startLocation = left->startLocation;
			lexer.NextCompoundToken();

			auto right = ParseExpression(state, leftBindPower);
			if (!right)
			{
				state.GetErrorTracker().AddError(
					opToken.GetStartLocation(), "Expected expression after '&&'", ErrorSeverity::Error);
			}
			else
			{
				node->m_RightOperand = right;
				node->endLocation = right->endLocation;
			}
			return node;
		}

		// --- All other binary operators: +, -, *, /, %, **, <, >, <=, >=, ==, !=, |, ^, &, <<, >> ---
		{
			auto node = CreateRef<BinaryExpressionNode>();
			node->m_LeftOperand = left;
			node->m_OperatorToken = opToken;
			node->startLocation = left->startLocation;
			lexer.NextCompoundToken(); // consume operator

			auto right = ParseExpression(state, leftBindPower);
			if (!right)
			{
				state.GetErrorTracker().AddError(
					opToken.GetStartLocation(),
					Core::StringFormat::Format("Expected expression after '{0}'", opToken.GetText()),
					ErrorSeverity::Error);
			}
			else
			{
				node->m_RightOperand = right;
				node->endLocation = right->endLocation;
			}
			return node;
		}
	}

	// =========================================================================
	// Postfix parsing - chained operators: (), [], ., ->, ::, ++, --
	// =========================================================================
} // namespace Scripting
