#include "ExpressionStatementNode.h"
#include "Parser/PrattParser.h"
#include "Lexer/Lexer.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	Ref<ExpressionStatementNode> ExpressionStatementNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		// Speculative parse inside undo scope. If valid, commit and return node.
		auto undo = LexerUndoScope(lexer);

		auto expr = PrattParser::ParseExpression(state);
		if (expr == nullptr)
			return nullptr; // not an expression start - undo restores position

		Token next = lexer.PeekCompound();
		if (next.GetType() != TokenType::SemiColon)
			return nullptr; // expression without semicolon - not this node type, undo restores

		auto node = CreateRef<ExpressionStatementNode>();
		node->m_Expression = std::move(expr);
		node->startLocation = node->m_Expression->startLocation;

		// Consume semicolon
		Token token = lexer.PeekCompound();
		node->m_SemicolonToken = token;
		node->endLocation = token.GetEndLocation();
		lexer.NextCompoundToken();

		undo.Commit();
		return node;
	}

	void ExpressionStatementNode::ParseBody(ParserState& state)
	{
		// Body fully parsed inline in TryParse (same pattern as AnnotationNode).
		// This method exists for API consistency but is not called by TryParse.
	}

	std::span<const Ref<AstNode>> ExpressionStatementNode::Children() const
	{
		if (m_Expression != nullptr)
			return std::span<const Ref<AstNode>>(&m_Expression, 1);
		return {};
	}

	Core::String ExpressionStatementNode::ToString() const
	{
		if (m_Expression != nullptr)
			return Core::StringFormat::Format("{0};", m_Expression->ToString());
		return ";";
	}


} // namespace Scripting
