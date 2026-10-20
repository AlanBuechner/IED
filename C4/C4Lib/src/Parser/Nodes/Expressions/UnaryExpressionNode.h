#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Unary operator expression: OP operand (prefix) or operand OP (postfix).
	struct UnaryExpressionNode : public AstNode
	{
		UnaryExpressionNode() : m_IsPostfix(false) {}

		const Ref<AstNode>& GetOperand() const { return m_Operand; }
		const Token& GetOperatorToken() const { return m_OperatorToken; }
		bool IsPostfix() const { return m_IsPostfix; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::UnaryExpression; }

		friend struct PrattParser;

	private:
		Ref<AstNode> m_Operand;
		Token m_OperatorToken;
		bool m_IsPostfix;
	};
}
