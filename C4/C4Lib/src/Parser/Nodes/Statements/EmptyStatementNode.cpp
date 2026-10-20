#include "EmptyStatementNode.h"

namespace Scripting
{

	Ref<EmptyStatementNode> EmptyStatementNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token token = lexer.PeekCompound();
		if (token.GetType() != TokenType::SemiColon)
			return nullptr;

		auto node = CreateRef<EmptyStatementNode>();
		node->ParseBody(state);
		return node;
	}

	void EmptyStatementNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token token = lexer.PeekCompound();
		m_SemicolonToken = token;
		startLocation = token.GetStartLocation();
		endLocation = token.GetEndLocation();

		lexer.NextCompoundToken();
	}

} // namespace Scripting
