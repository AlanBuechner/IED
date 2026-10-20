#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Binary operator expression: left OP right.
	/// Created inline by the Pratt parser for arithmetic, comparison, equality, bitwise, and shift operators.
	struct BinaryExpressionNode : public AstNode
	{
		BinaryExpressionNode() = default;

		const Ref<AstNode>& GetLeftOperand() const { return m_LeftOperand; }
		const Token& GetOperatorToken() const { return m_OperatorToken; }
		const Ref<AstNode>& GetRightOperand() const { return m_RightOperand; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::BinaryExpression; }

		friend struct PrattParser;

	private:
		Ref<AstNode> m_LeftOperand;
		Token m_OperatorToken;
		Ref<AstNode> m_RightOperand;
		mutable Ref<AstNode> m_Children[2] = { nullptr, nullptr };
	};
}
