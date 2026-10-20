#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Represents a bare semicolon `;` (empty/null statement).
	struct EmptyStatementNode : public AstNode
	{
		EmptyStatementNode() = default;

		const Token& GetSemicolonToken() const { return m_SemicolonToken; }

		/// Factory: peeks for `;`. Creates a node and calls ParseBody.
		/// Returns nullptr if current token is not `;` (no tokens consumed).
		static Ref<EmptyStatementNode> TryParse(ParserState& state);

		/// Consumes the semicolon token and sets locations.
		void ParseBody(ParserState& state);

		Core::String ToString() const override { return ";"; }

	protected:
		NodeType Type() const override { return NodeType::EmptyStatement; }

	private:
		Token m_SemicolonToken;
	};
}
