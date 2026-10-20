#include "NewExpressionNode.h"
#include "../Declarations/QualTypeNode.h"
#include "Parser/PrattParser.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	namespace
	{
		Core::String JoinArguments(const Core::Vector<Ref<AstNode>>& args)
		{
			Core::String result;
			for (uint32 i = 0; i < args.Count(); ++i)
			{
				if (i > 0) result += ", ";
				result += args[i] ? args[i]->ToString() : "";
			}
			return result;
		}
	}

	std::span<const Ref<AstNode>> NewExpressionNode::Children() const
	{
		return m_Children;
	}

	Core::String NewExpressionNode::ToString() const
	{
		Core::String result = "new ";
		if (m_Placement) result += "(" + m_Placement->ToString() + ") ";
		result += m_Type ? m_Type->ToString() : "";
		if (m_HasBraces) return result + "{" + JoinArguments(m_Arguments) + "}";
		return result + "(" + JoinArguments(m_Arguments) + ")";
	}

	void NewExpressionNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		Token newToken = lexer.PeekCompound();
		startLocation = newToken.GetStartLocation();
		lexer.NextCompoundToken();

		if (lexer.PeekCompound().GetType() == TokenType::OpenParen && !ParsePlacement(state)) return;

		m_Type = QualTypeNode::TryParse(state);
		if (!m_Type)
		{
			state.GetErrorTracker().AddError(lexer.PeekCompound().GetStartLocation(), "Expected a type after 'new'", ErrorSeverity::Error);
			return;
		}

		Token next = lexer.PeekCompound();
		if (next.GetType() == TokenType::OpenParen) ParseArguments(state, TokenType::CloseParen, ")");
		else if (next.GetType() == TokenType::OpenCurl)
		{
			m_HasBraces = true;
			ParseArguments(state, TokenType::CloseCurl, "}");
		}
		else endLocation = m_Type->endLocation; // `new T`: no arguments
		CollectChildren();
	}

	bool NewExpressionNode::ParsePlacement(ParserState& state)
	{
		// `new (storage) T(...)`: the object is built in the given storage instead of on the heap.
		Lexer& lexer = state.GetLexer();
		lexer.NextCompoundToken();
		m_Placement = PrattParser::ParseExpression(state);
		Token close = lexer.PeekCompound();
		if (!m_Placement || close.GetType() != TokenType::CloseParen)
		{
			state.GetErrorTracker().AddError(close.GetStartLocation(),
				"Expected 'new (storage) Type(...)': the storage address in parentheses, then the type", ErrorSeverity::Error);
			return false;
		}
		lexer.NextCompoundToken();
		return true;
	}

	void NewExpressionNode::ParseArguments(ParserState& state, TokenType close, const char* closeText)
	{
		Lexer& lexer = state.GetLexer();
		lexer.NextCompoundToken(); // the opening ( or {
		while (lexer.PeekCompound().GetType() != close)
		{
			auto arg = PrattParser::ParseArgument(state);
			if (!arg) break;
			m_Arguments.Push(arg);
			if (lexer.PeekCompound().GetType() != TokenType::Comma) break;
			lexer.NextCompoundToken(); // a trailing comma is allowed
		}
		Token next = lexer.PeekCompound();
		if (next.GetType() != close)
		{
			state.GetErrorTracker().AddError(next.GetStartLocation(),
				Core::StringFormat::Format("Expected '{0}' to close new expression, got '{1}'", closeText, next.GetText()), ErrorSeverity::Error);
			return;
		}
		endLocation = next.GetEndLocation();
		lexer.NextCompoundToken();
	}

	void NewExpressionNode::CollectChildren()
	{
		if (m_Placement) m_Children.Push(m_Placement);
		for (const auto& a : m_Arguments) m_Children.Push(a);
	}
}
