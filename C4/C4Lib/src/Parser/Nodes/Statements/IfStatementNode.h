#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	struct CodeBlockNode;

	/// If statement with optional else-if chain and else branch.
	/// Grammar: 'if' '(' expression ')' body ['else' (if_statement | body)]
	struct IfStatementNode : public AstNode
	{
		IfStatementNode() = default;

		const Token& GetIfToken() const { return m_IfToken; }
		const Ref<AstNode>& GetCondition() const { return m_Condition; }
		const Ref<CodeBlockNode>& GetThenBody() const { return m_ThenBody; }
		const Ref<AstNode>& GetThenSingleStatement() const { return m_ThenSingleStatement; }
		const Ref<AstNode>& GetElseBranch() const { return m_ElseBranch; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override { return "if"; }
static Ref<IfStatementNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::IfStatement; }

	private:
		Token m_IfToken;
		Token m_OpenParenToken;
		Ref<AstNode> m_Condition;
		Token m_CloseParenToken;
		Ref<CodeBlockNode> m_ThenBody;
		Ref<AstNode> m_ThenSingleStatement;
		Ref<AstNode> m_ElseBranch;
		Core::Vector<Ref<AstNode>> m_Children; // condition + then + else
	};
}
