#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Parenthesized expression: (expression).
	struct ParenthesizedExpressionNode : public AstNode
	{
		ParenthesizedExpressionNode() = default;

		const Token& GetOpenParenToken() const { return m_OpenParenToken; }
		const Ref<AstNode>& GetExpression() const { return m_Expression; }
		const Token& GetCloseParenToken() const { return m_CloseParenToken; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::ParenthesizedExpression; }

		friend struct PrattParser;

	private:
		Token m_OpenParenToken;
		Ref<AstNode> m_Expression;
		Token m_CloseParenToken;
	};
}
