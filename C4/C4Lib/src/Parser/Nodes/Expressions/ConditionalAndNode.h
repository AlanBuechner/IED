#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Logical AND expression: left && right.
	struct ConditionalAndNode : public AstNode
	{
		ConditionalAndNode() = default;

		const Ref<AstNode>& GetLeftOperand() const { return m_Children[0]; }
		const Token& GetOperatorToken() const { return m_OperatorToken; }
		const Ref<AstNode>& GetRightOperand() const { return m_Children[1]; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::ConditionalAnd; }

		friend struct PrattParser;

	private:
		Token m_OperatorToken;
		Ref<AstNode> m_Children[2]; // the left operand, then the right: Children is a span of them
	};
}
