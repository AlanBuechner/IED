#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Ternary conditional expression: condition ? thenBranch : elseBranch.
	struct TernaryExpressionNode : public AstNode
	{
		TernaryExpressionNode() = default;

		const Ref<AstNode>& GetCondition() const { return m_Children[0]; }
		const Token& GetQuestionToken() const { return m_QuestionToken; }
		const Ref<AstNode>& GetThenBranch() const { return m_Children[1]; }
		const Token& GetColonToken() const { return m_ColonToken; }
		const Ref<AstNode>& GetElseBranch() const { return m_Children[2]; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::TernaryExpression; }

		friend struct PrattParser;

	private:
		Token m_QuestionToken;
		Token m_ColonToken;
		Ref<AstNode> m_Children[3]; // the condition, then the two branches: Children is a span of them
	};
}
