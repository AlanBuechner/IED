#include "Parser/PrattParser.h"
#include "Lexer/Lexer.h"
#include "Core/StringFormat.h"
#include "Model/TypeStore.h"
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
#include "Nodes/Expressions/TypeOfExpressionNode.h"
#include "Nodes/Expressions/VaArgExpressionNode.h"
#include "Nodes/Expressions/ParenthesizedExpressionNode.h"
#include "Nodes/Expressions/LiteralNode.h"
#include "Nodes/Expressions/IdentifierExpressionNode.h"
#include "Nodes/Expressions/BracedInitializerNode.h"
#include "Nodes/Expressions/LambdaNode.h"
#include "Nodes/Declarations/QualTypeNode.h"
#include "Nodes/Declarations/NamespaceQualifiedNameNode.h"

namespace Scripting
{
	bool PrattParser::IsFoldOperator(TokenType type)
	{
		return type == TokenType::Comma || InfixBindPower(type) > 0; // `(F(args), ...)` calls F per element
	}

	TokenType PrattParser::FoldOperator(Lexer& lexer)
	{
		// The operator next to a `...` at this nesting level, before the closing ')'; None when there is none.
		// Peeks only.
		auto undo = LexerUndoScope(lexer);
		int depth = 0;
		Token previous;
		TokenType op = TokenType::None;
		for (uint32 i = 0; op == TokenType::None; ++i)
		{
			const Token t = lexer.PeekCompound(i);
			const TokenType type = t.GetType();
			if (type == TokenType::EndOfFile || type == TokenType::None) break;
			if (type == TokenType::OpenParen || type == TokenType::OpenBracket || type == TokenType::OpenCurl) ++depth;
			else if (type == TokenType::CloseParen || type == TokenType::CloseBracket || type == TokenType::CloseCurl)
			{
				if (depth == 0) break;
				--depth;
			}
			else if (type == TokenType::Ellipsis && depth == 0)
			{
				const TokenType next = lexer.PeekCompound(i + 1).GetType();
				if (i == 0 && IsFoldOperator(next)) op = next;                          // (... op pack)
				else if (i > 0 && IsFoldOperator(previous.GetType())) op = previous.GetType(); // (pack op ...)
			}
			previous = t;
		}
		undo.Cancel();
		return op;
	}

	Ref<AstNode> PrattParser::ParseFold(ParserState& state, const Token& openParen)
	{
		Lexer& lexer = state.GetLexer();
		const TokenType op = FoldOperator(lexer);
		if (op == TokenType::None) return nullptr;
		ErrorTracker& errors = state.GetErrorTracker();
		auto expect = [&](TokenType type, const char* what) {
			Token t = lexer.PeekCompound();
			if (t.GetType() != type)
				errors.AddError(t.GetStartLocation(), Core::StringFormat::Format("Expected {0} in the fold expression, got '{1}'", what, t.GetText()), ErrorSeverity::Error);
			else lexer.NextCompoundToken();
			return t;
		};
		// An operand of a fold binds tighter than its operator (C++: a cast-expression).
		const uint32 operand = InfixBindPower(op) + 1;
		Ref<AstNode> first;
		Token opToken;
		if (lexer.PeekCompound().GetType() != TokenType::Ellipsis)
		{
			first = ParseExpression(state, operand);
			opToken = expect(op, "the operator before '...'");
		}
		expect(TokenType::Ellipsis, "'...'");
		Ref<AstNode> last;
		if (!first || lexer.PeekCompound().GetType() != TokenType::CloseParen)
		{
			opToken = expect(op, "the same operator after '...'");
			last = ParseExpression(state, operand);
		}
		const Token close = expect(TokenType::CloseParen, "')'");
		return FoldExpressionNode::Of(opToken, first, last, openParen.GetStartLocation(), close.GetEndLocation());
	}

	namespace
	{
		// `(v)` that names something a value could be: a plain or qualified name that is not a built-in type or a
		// template's type parameter.
		bool CouldBeAValue(const ParserState& state, const QualTypeNode& type)
		{
			if (type.IsLeadingConst() || type.IsPacked() || type.IndirectionCount() > 0 || type.HasTemplateArgs() ||
				type.FuncPtrType() || type.IsReturnList() || !type.BaseName())
				return false;
			const auto& segments = type.BaseName()->GetSegments();
			if (segments.Empty()) return false;
			const Core::String& name = segments.Back().nameToken.GetText();
			return !TypeStore::IsBuiltinName(name) && !(segments.Count() == 1 && state.IsTypeParameter(name));
		}

		// Both a binary operator and a prefix one: after `(v)` it continues an expression.
		bool IsBinaryOrPrefix(TokenType type)
		{
			return type == TokenType::Plus || type == TokenType::Minus || type == TokenType::Star || type == TokenType::DoubleStar ||
				type == TokenType::Ampersand || type == TokenType::DoubleAmpersand;
		}
	}

	Ref<AstNode> PrattParser::ParseParenOrCStyleCast(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		Token openParen = lexer.PeekCompound();

		// Speculative parse: try to parse as a type inside the parentheses.
		auto undo = LexerUndoScope(lexer);
		lexer.NextCompoundToken(); // consume (

		state.GetErrorTracker().SuppressErrors();
		auto parsedType = QualTypeNode::TryParse(state);

		Token afterType = lexer.PeekCompound();

		if (parsedType && afterType.GetType() == TokenType::CloseParen)
		{
			// Possible C-style cast: (Type)expr
			lexer.NextCompoundToken(); // consume )

			// A parenthesized name followed by `+ - * ** & &&` is an operand of that operator (C#'s rule): `(v) * (v)`
			// multiplies; a cast of such an operand is written `(T)(-x)`.
			const bool operandOfOperator = CouldBeAValue(state, *parsedType) && IsBinaryOrPrefix(lexer.PeekCompound().GetType());

			// A cast is a prefix operator: `(int)f + 1` is `((int)f) + 1`.
			auto operand = operandOfOperator ? nullptr : ParseExpression(state, BindPower::PrefixOperand);

			if (operand)
			{
				// Confirmed C-style cast - flush the speculative type's diagnostics.
				state.GetErrorTracker().CommitSuppressedErrors();

				auto node = CreateRef<CastExpressionNode>();
				node->m_Kind = CastExpressionNode::CastKind::CStyle;
				node->m_CastKeywordToken = openParen;
				node->m_TargetType = parsedType;
				node->m_Operand = operand;
				node->startLocation = openParen.GetStartLocation();
				node->endLocation = operand->endLocation;

				undo.Commit();
				return node;
			}

			// No operand after (Type) - not a C-style cast, fall through to parenthesized expression parsing.
			// Do NOT cancel here; the outer check below handles rollback.
		}

		// Not a confirmed C-style cast - roll back speculative parse before re-parsing as parenthesized expr.
		if (!undo.IsCommitted())
		{
			state.GetErrorTracker().DiscardSuppressedErrors();
			undo.Cancel();
		}

		// Parse as parenthesized expression from scratch, lexer is at '(' again.
		auto node = CreateRef<ParenthesizedExpressionNode>();
		node->m_OpenParenToken = lexer.PeekCompound();
		node->startLocation = node->m_OpenParenToken.GetStartLocation();
		lexer.NextCompoundToken(); // consume (
		if (auto fold = ParseFold(state, openParen)) return fold;

		auto expr = ParseExpression(state, 0);
		if (!expr)
		{
			state.GetErrorTracker().AddError(
				openParen.GetStartLocation(),
				"Expected expression inside parentheses", ErrorSeverity::Error);
		}
		else
			node->m_Expression = expr;

		Token close = lexer.PeekCompound();
		if (close.GetType() != TokenType::CloseParen)
		{
			state.GetErrorTracker().AddError(
				close.GetStartLocation(),
				Core::StringFormat::Format("Expected ')' to close parenthesized expression, got '{0}'", close.GetText()),
				ErrorSeverity::Error);
		}
		else
		{
			node->m_CloseParenToken = close;
			node->endLocation = close.GetEndLocation();
			lexer.NextCompoundToken(); // consume )
		}

		return node;
	}

	// =========================================================================
	// C++-style cast parsing: static_cast<T>(expr), etc.
	// =========================================================================

	Ref<AstNode> PrattParser::ParseCppCast(ParserState& state, Token keywordToken)
	{
		Lexer& lexer = state.GetLexer();
		auto node = CreateRef<CastExpressionNode>();
		node->m_CastKeywordToken = keywordToken;
		node->startLocation = keywordToken.GetStartLocation();

		const Core::String& text = keywordToken.GetText();
		if (text == "static_cast")       node->m_Kind = CastExpressionNode::CastKind::StaticCast;
		else if (text == "dynamic_cast")  node->m_Kind = CastExpressionNode::CastKind::DynamicCast;
		else if (text == "reinterpret_cast") node->m_Kind = CastExpressionNode::CastKind::ReinterpretCast;
		else if (text == "const_cast")    node->m_Kind = CastExpressionNode::CastKind::ConstCast;

		lexer.NextCompoundToken(); // consume cast keyword

		// Expect < for template-style type argument
		Token lt = lexer.PeekCompound();
		if (lt.GetType() != TokenType::Less)
		{
			state.GetErrorTracker().AddError(
				lt.GetStartLocation(),
				Core::StringFormat::Format("Expected '<' after '{0}', got '{1}'", text, lt.GetText()),
				ErrorSeverity::Error);
			return node;
		}
		lexer.NextCompoundToken(); // consume <

		state.GetErrorTracker().SuppressErrors();
		auto targetType = QualTypeNode::TryParse(state);
		if (targetType)
			state.GetErrorTracker().CommitSuppressedErrors();
		else
			state.GetErrorTracker().DiscardSuppressedErrors();

		if (!targetType)
		{
			Token bad = lexer.PeekCompound();
			state.GetErrorTracker().AddError(
				bad.GetStartLocation(),
				Core::StringFormat::Format("Expected type in '{0}', got '{1}'", text, bad.GetText()),
				ErrorSeverity::Error);
		}
		else
			node->m_TargetType = targetType;

		// Expect >
		Token gt = lexer.PeekCompound();
		if (gt.GetType() != TokenType::Greater)
		{
			state.GetErrorTracker().AddError(
				gt.GetStartLocation(),
				Core::StringFormat::Format("Expected '>' in '{0}', got '{1}'", text, gt.GetText()),
				ErrorSeverity::Error);
		}
		else
			lexer.NextCompoundToken(); // consume >

		// Expect ( for operand
		Token open = lexer.PeekCompound();
		if (open.GetType() != TokenType::OpenParen)
		{
			state.GetErrorTracker().AddError(
				open.GetStartLocation(),
				Core::StringFormat::Format("Expected '(' after '{0}<Type>', got '{1}'", text, open.GetText()),
				ErrorSeverity::Error);
			if (targetType)
				node->endLocation = targetType->endLocation;
			return node;
		}
		lexer.NextCompoundToken(); // consume (

		auto operand = ParseExpression(state, 0);
		if (!operand)
		{
			state.GetErrorTracker().AddError(
				open.GetStartLocation(),
				Core::StringFormat::Format("Expected expression in '{0}'", text),
				ErrorSeverity::Error);
		}
		else
			node->m_Operand = operand;

		Token close = lexer.PeekCompound();
		if (close.GetType() != TokenType::CloseParen)
		{
			state.GetErrorTracker().AddError(
				close.GetStartLocation(),
				Core::StringFormat::Format("Expected ')' in '{0}', got '{1}'", text, close.GetText()),
				ErrorSeverity::Error);
		}
		else
		{
			node->endLocation = close.GetEndLocation();
			lexer.NextCompoundToken(); // consume )
		}

		return node;
	}
} // namespace Scripting
