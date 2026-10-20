#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	struct CodeBlockNode;

	/// While loop: 'while' '(' expression ')' body
	struct WhileLoopNode : public AstNode
	{
		WhileLoopNode() = default;

		const Token& GetWhileToken() const { return m_WhileToken; }
		const Ref<AstNode>& GetCondition() const { return m_Condition; }
		const Ref<CodeBlockNode>& GetBody() const { return m_Body; }
		const Ref<AstNode>& GetSingleStatementBody() const { return m_SingleStatementBody; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override { return "while"; }
static Ref<WhileLoopNode> TryParse(ParserState& state);
		void ParseBody(ParserState& state);

	protected:
		NodeType Type() const override { return NodeType::WhileLoop; }

	private:
		Token m_WhileToken;
		Token m_OpenParenToken;
		Ref<AstNode> m_Condition;
		Token m_CloseParenToken;
		Ref<CodeBlockNode> m_Body;
		Ref<AstNode> m_SingleStatementBody;
		Core::Vector<Ref<AstNode>> m_Children; // condition + body
	};
}
