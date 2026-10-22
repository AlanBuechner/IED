#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Represents an identifier reference in expression context.
	///
	/// Examples: `x`, `myVar`, `someFunction`
	///
	/// This is the most common expression leaf alongside LiteralNode.
	struct IdentifierExpressionNode : public AstNode
	{
		static constexpr NodeType kNodeType = NodeType::IdentifierExpression;
		IdentifierExpressionNode() = default;

		// --- Getters ---

		const Token& GetNameToken() const { return m_NameToken; }
		const Core::String& GetName() const { return m_NameToken.GetText(); }

		void SetNameToken(const Token& token) { m_NameToken = token; }

		/// Factory: peeks for TokenType::Identifier. Creates a node and calls ParseBody.
		/// Returns nullptr if current token is not an identifier (keywords like true/false/null
		/// are separate token types and won't match here).
		static Ref<IdentifierExpressionNode> TryParse(ParserState& state);

		/// Consumes the identifier token and sets endLocation.
		void ParseBody(ParserState& state);

		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::IdentifierExpression; }

	private:
		Token m_NameToken;
	};
}
