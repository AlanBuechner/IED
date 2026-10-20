#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Ternary conditional expression: condition ? thenBranch : elseBranch.
	struct TernaryExpressionNode : public AstNode
	{
		TernaryExpressionNode() = default;

		const Ref<AstNode>& GetCondition() const { return m_Condition; }
		const Token& GetQuestionToken() const { return m_QuestionToken; }
		const Ref<AstNode>& GetThenBranch() const { return m_ThenBranch; }
		const Token& GetColonToken() const { return m_ColonToken; }
		const Ref<AstNode>& GetElseBranch() const { return m_ElseBranch; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::TernaryExpression; }

		friend struct PrattParser;

	private:
		Ref<AstNode> m_Condition;
		Token m_QuestionToken;
		Ref<AstNode> m_ThenBranch;
		Token m_ColonToken;
		Ref<AstNode> m_ElseBranch;
		mutable Ref<AstNode> m_Children[3] = { nullptr, nullptr, nullptr };
	};
}
