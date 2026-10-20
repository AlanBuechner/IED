#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Represents a block of executable code enclosed in `{}`.
	/// Used as function bodies, loop bodies, if/else branches, standalone scopes, etc.
	struct CodeBlockNode : public AstNode
	{
		CodeBlockNode() = default;

		const Token& GetOpenBraceToken() const { return m_OpenBraceToken; }
		const Token& GetCloseBraceToken() const { return m_CloseBraceToken; }
		const Core::Vector<Ref<AstNode>>& GetStatements() const { return m_Statements; }

		bool HasBraces() const { return m_OpenBraceToken.GetType() != TokenType::None; }
		uint32 StatementCount() const { return m_Statements.Count(); }

		/// Factory: peeks for `{`. If present, commits and calls ParseBody.
		/// Returns nullptr if current token is not `{` (no tokens consumed).
		static Ref<CodeBlockNode> TryParse(ParserState& state);

		/// Consumes `{`, parses child statements, consumes `}`.
		void ParseBody(ParserState& state);

		std::span<const Ref<AstNode>> Children() const override { return m_Statements; }
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::CodeBlock; }

	private:
		Token m_OpenBraceToken;
		Core::Vector<Ref<AstNode>> m_Statements;
		Token m_CloseBraceToken;
	};
}
