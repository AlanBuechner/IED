#include "VaArgExpressionNode.h"
#include "Parser/PrattParser.h"
#include "Lexer/Lexer.h"

namespace Scripting
{
	bool VaArgExpressionNode::StartsHere(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		const Token name = lexer.PeekCompound();
		return name.GetType() == TokenType::Identifier && name.GetText() == "va_arg" && lexer.PeekCompound(1).GetType() == TokenType::OpenParen;
	}

	Ref<VaArgExpressionNode> VaArgExpressionNode::TryParse(ParserState& state)
	{
		if (!StartsHere(state)) return nullptr;
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errors = state.GetErrorTracker();
		auto node = CreateRef<VaArgExpressionNode>();
		node->startLocation = lexer.PeekCompound().GetStartLocation();
		lexer.NextCompoundToken(); // va_arg
		lexer.NextCompoundToken(); // (
		node->m_List = PrattParser::ParseExpression(state, 0);
		if (lexer.PeekCompound().GetType() != TokenType::Comma)
		{
			errors.AddError(lexer.PeekCompound().GetStartLocation(), "va_arg(list, T) takes the list and the type to read", ErrorSeverity::Error);
			return node;
		}
		lexer.NextCompoundToken(); // ,
		node->m_ArgumentType = CreateRef<QualTypeNode>();
		node->m_ArgumentType->ParseBody(state);
		const Token close = lexer.PeekCompound();
		if (close.GetType() != TokenType::CloseParen)
		{
			errors.AddError(close.GetStartLocation(), "Expected ')' after the type of va_arg, got '" + close.GetText() + "'", ErrorSeverity::Error);
			return node;
		}
		node->endLocation = close.GetEndLocation();
		lexer.NextCompoundToken();
		if (node->m_List) node->m_Children.Push(node->m_List);
		node->m_Children.Push(node->m_ArgumentType);
		return node;
	}

	Core::String VaArgExpressionNode::ToString() const
	{
		return "va_arg(" + (m_List ? m_List->ToString() : Core::String("?")) + ", " + (m_ArgumentType ? m_ArgumentType->ToString() : Core::String("?")) + ")";
	}
}
