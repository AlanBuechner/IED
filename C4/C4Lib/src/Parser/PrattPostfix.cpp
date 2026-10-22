#include "Parser/PrattParser.h"
#include "Lexer/Lexer.h"
#include "Core/StringFormat.h"
#include "Nodes/Expressions/BinaryExpressionNode.h"
#include "Nodes/Expressions/UnaryExpressionNode.h"
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
#include "Nodes/Expressions/LiteralNode.h"
#include "Nodes/Expressions/IdentifierExpressionNode.h"
#include "Nodes/Expressions/BracedInitializerNode.h"
#include "Nodes/Expressions/LambdaNode.h"
#include "Nodes/Expressions/NewExpressionNode.h"
#include "Nodes/Declarations/QualTypeNode.h"
#include "Nodes/Declarations/NamespaceQualifiedNameNode.h"

namespace Scripting
{
	Ref<AstNode> PrattParser::ParsePostfix(ParserState& state, Ref<AstNode> base)
	{
		Lexer& lexer = state.GetLexer();

		while (true)
		{
			Token next = lexer.PeekCompound();
			TokenType type = next.GetType();

			if (type == TokenType::EndOfFile || type == TokenType::None)
				break;

			// --- Braced initializer: base{x, y} ---
			if (type == TokenType::OpenCurl)
			{
				auto braceInit = CreateRef<BracedInitializerNode>();
				braceInit->ParseBody(state);

				// Chain as binary expression for now; semantic analysis will resolve
				auto wrapper = CreateRef<BinaryExpressionNode>();
				wrapper->m_LeftOperand = base;
				wrapper->m_RightOperand = braceInit;
				wrapper->startLocation = base->startLocation;
				wrapper->endLocation = braceInit->endLocation;
				base = wrapper;
				continue;
			}

			// --- Function call: base(args) ---
			if (type == TokenType::OpenParen)
			{
				auto node = CreateRef<FunctionCallNode>();
				node->SetCallee(base);
				node->m_OpenParenToken = next;
				node->startLocation = base->startLocation;
				lexer.NextCompoundToken(); // consume (

				// Parse arguments separated by commas
				Token peek = lexer.PeekCompound();
				if (peek.GetType() != TokenType::CloseParen)
				{
					while (true)
					{
						auto arg = ParseArgument(state);
						if (!arg)
						{
							state.GetErrorTracker().AddError(
								peek.GetStartLocation(),
								Core::StringFormat::Format("Expected argument expression, got '{0}'", peek.GetText()),
								ErrorSeverity::Error);
							break;
						}
						node->AddArgument(arg);

						peek = lexer.PeekCompound();
						if (peek.GetType() == TokenType::Comma)
						{
							lexer.NextCompoundToken(); // consume comma
							peek = lexer.PeekCompound();
							if (peek.GetType() == TokenType::CloseParen)
								break; // trailing comma
							continue;
						}
						break;
					}
				}

				Token close = lexer.PeekCompound();
				if (close.GetType() != TokenType::CloseParen)
				{
					state.GetErrorTracker().AddError(
						close.GetStartLocation(),
						Core::StringFormat::Format("Expected ')' to close function call, got '{0}'", close.GetText()),
						ErrorSeverity::Error);
				}
				else
				{
					node->m_CloseParenToken = close;
					node->endLocation = close.GetEndLocation();
					lexer.NextCompoundToken(); // consume )
				}

				base = node;
				continue;
			}

			// --- Array index: base[index] ---
			if (type == TokenType::OpenBracket)
			{
				auto node = CreateRef<ArrayIndexNode>();
				node->m_ArrayExpression = base;
				node->m_OpenBracketToken = next;
				node->startLocation = base->startLocation;
				lexer.NextCompoundToken(); // consume [

				auto index = ParseExpression(state, 0);
				if (!index)
				{
					Token bad = lexer.PeekCompound();
					state.GetErrorTracker().AddError(
						bad.GetStartLocation(),
						Core::StringFormat::Format("Expected index expression, got '{0}'", bad.GetText()),
						ErrorSeverity::Error);
				}
				else
					node->m_IndexExpression = index;

				Token close = lexer.PeekCompound();
				if (close.GetType() != TokenType::CloseBracket)
				{
					state.GetErrorTracker().AddError(
						close.GetStartLocation(),
						Core::StringFormat::Format("Expected ']' to close index, got '{0}'", close.GetText()),
						ErrorSeverity::Error);
				}
				else
				{
					node->m_CloseBracketToken = close;
					node->endLocation = close.GetEndLocation();
					lexer.NextCompoundToken(); // consume ]
				}

				base = node;
				continue;
			}

			// --- Member access: base.member or base->member ---
			if (type == TokenType::Dot || type == TokenType::Arrow)
			{
				auto node = CreateRef<MemberAccessNode>();
				node->m_BaseObject = base;
				node->m_AccessOperatorToken = next;
				node->startLocation = base->startLocation;
				lexer.NextCompoundToken(); // consume . or ->

				Token memberName = lexer.PeekCompound();
				if (memberName.GetType() != TokenType::Identifier)
				{
					state.GetErrorTracker().AddError(
						memberName.GetStartLocation(),
						Core::StringFormat::Format("Expected identifier for member name, got '{0}'", memberName.GetText()),
						ErrorSeverity::Error);
					node->m_MemberNameToken = memberName;
				}
				else
				{
					node->m_MemberNameToken = memberName;
					node->endLocation = memberName.GetEndLocation();
					lexer.NextCompoundToken(); // consume identifier
				}

				// `obj.Base::F()`: the names before the last `::` qualify the member (B35).
				while (node->m_MemberNameToken.GetType() == TokenType::Identifier && lexer.PeekCompound().GetType() == TokenType::DoubleColon)
				{
					lexer.NextCompoundToken(); // consume ::
					Token qualified = lexer.PeekCompound();
					if (qualified.GetType() != TokenType::Identifier)
					{
						state.GetErrorTracker().AddError(qualified.GetStartLocation(),
							Core::StringFormat::Format("Expected a member name after '::', got '{0}'", qualified.GetText()), ErrorSeverity::Error);
						break;
					}
					node->m_Qualifier.Push(node->m_MemberNameToken);
					node->m_MemberNameToken = qualified;
					node->endLocation = qualified.GetEndLocation();
					lexer.NextCompoundToken();
				}

				// `p.F<int32>(x)`: a member template called with written arguments (BUG-117).
				if (auto call = TryMemberTemplateCall(state, node)) { base = call; continue; }
				base = node;
				continue;
			}

			// --- Static/scope resolution: base::member ---
			if (type == TokenType::DoubleColon)
			{
				auto node = CreateRef<StaticAccessNode>();
				node->m_BaseType = base;
				node->m_ScopeResolutionToken = next;
				node->startLocation = base->startLocation;
				lexer.NextCompoundToken(); // consume ::

				Token memberName = lexer.PeekCompound();
				if (memberName.GetType() != TokenType::Identifier)
				{
					state.GetErrorTracker().AddError(
						memberName.GetStartLocation(),
						Core::StringFormat::Format("Expected identifier after '::', got '{0}'", memberName.GetText()),
						ErrorSeverity::Error);
					node->m_MemberNameToken = memberName;
				}
				else
				{
					node->m_MemberNameToken = memberName;
					node->endLocation = memberName.GetEndLocation();
					lexer.NextCompoundToken();
				}

				base = node;
				continue;
			}

			// --- Postfix ++ and -- ---
			if (type == TokenType::DoublePlus || type == TokenType::DoubleMinus)
			{
				auto node = CreateRef<UnaryExpressionNode>();
				node->m_Operand = base;
				node->m_OperatorToken = next;
				node->m_IsPostfix = true;
				node->startLocation = base->startLocation;
				node->endLocation = next.GetEndLocation();
				lexer.NextCompoundToken(); // consume ++ or --

				base = node;
				continue;
			}

			// Not a postfix operator - stop chaining
			break;
		}

		return base;
	}

	// =========================================================================
	// Parenthesized expression vs C-style cast disambiguation
	// =========================================================================
} // namespace Scripting
