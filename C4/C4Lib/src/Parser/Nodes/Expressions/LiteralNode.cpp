#include "LiteralNode.h"

namespace Scripting
{

	Ref<LiteralNode> LiteralNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token token = lexer.PeekCompound();
		TokenType type = token.GetType();

		if (type != TokenType::Number &&
			type != TokenType::Text &&
			type != TokenType::Char &&
			type != TokenType::True &&
			type != TokenType::False &&
			type != TokenType::Null)
		{
			return nullptr; // not a literal expression
		}

		auto node = CreateRef<LiteralNode>();
		node->ParseBody(state);
		return node;
	}

	void LiteralNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token token = lexer.PeekCompound();
		startLocation = token.GetStartLocation();
		endLocation = token.GetEndLocation();
		m_ValueToken = token;

		switch (token.GetType())
		{
		case TokenType::Text:   m_Kind = LiteralKind::String; break;
		case TokenType::Char:   m_Kind = LiteralKind::Char; break;
		case TokenType::True:
		case TokenType::False:  m_Kind = LiteralKind::Boolean; break;
		case TokenType::Null:   m_Kind = LiteralKind::Null; break;
		default:                m_Kind = LiteralKind::Number; break;
		}

		lexer.NextCompoundToken(); // consume the literal token
		if (m_Kind == LiteralKind::String)
			JoinAdjacentStrings(state);
	}

	void LiteralNode::JoinAdjacentStrings(ParserState& state)
	{
		// `"ab" "cd"` is one literal "abcd", as in C; both pieces must have the same prefix.
		Lexer& lexer = state.GetLexer();
		while (lexer.PeekCompound().GetType() == TokenType::Text)
		{
			const Token next = lexer.NextCompoundToken();
			if (next.GetLiteralFacts().unitWidth != GetUnitWidth())
				state.GetErrorTracker().AddError(next.GetStartLocation(),
					"adjacent string literals join only when they have the same prefix (none or u8, u, U)", ErrorSeverity::Error);
			m_ValueToken.SetText(m_ValueToken.GetText() + next.GetText());
			endLocation = next.GetEndLocation();
		}
	}

	Core::String LiteralNode::ToString() const
	{
		return m_ValueToken.GetText();
	}

} // namespace Scripting
