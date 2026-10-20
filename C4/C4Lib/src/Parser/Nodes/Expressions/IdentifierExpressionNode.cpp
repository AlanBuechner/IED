#include "IdentifierExpressionNode.h"

namespace Scripting
{

	Ref<IdentifierExpressionNode> IdentifierExpressionNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token token = lexer.PeekCompound();
		if (token.GetType() != TokenType::Identifier)
			return nullptr;

		auto node = CreateRef<IdentifierExpressionNode>();
		node->ParseBody(state);
		return node;
	}

	void IdentifierExpressionNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token token = lexer.PeekCompound();
		m_NameToken = token;
		startLocation = token.GetStartLocation();
		endLocation = token.GetEndLocation();

		lexer.NextCompoundToken();
	}

	Core::String IdentifierExpressionNode::ToString() const
	{
		return m_NameToken.GetText();
	}

} // namespace Scripting
