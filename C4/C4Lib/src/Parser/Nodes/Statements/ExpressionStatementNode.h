#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Represents an expression followed by a semicolon.
	/// The most common statement type: function calls, assignments, increments, etc.
	struct ExpressionStatementNode : public AstNode
	{
		ExpressionStatementNode() = default;

		const Ref<AstNode>& GetExpression() const { return m_Expression; }
		const Token& GetSemicolonToken() const { return m_SemicolonToken; }

		/// Factory: speculatively parses an expression. If successful AND the next
		/// token is `;`, commits and calls ParseBody. Returns nullptr otherwise
		/// (no tokens consumed on failure).
		static Ref<ExpressionStatementNode> TryParse(ParserState& state);

		/// Parses the expression, consumes the semicolon, and sets locations.
		void ParseBody(ParserState& state);

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::ExpressionStatement; }

	private:
		Ref<AstNode> m_Expression;
		Token m_SemicolonToken;
	};
}
