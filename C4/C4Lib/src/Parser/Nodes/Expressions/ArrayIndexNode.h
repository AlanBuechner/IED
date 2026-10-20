#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Array/map index expression: arrayExpression[indexExpression].
	struct ArrayIndexNode : public AstNode
	{
		ArrayIndexNode() = default;

		const Ref<AstNode>& GetArrayExpression() const { return m_ArrayExpression; }
		const Token& GetOpenBracketToken() const { return m_OpenBracketToken; }
		const Ref<AstNode>& GetIndexExpression() const { return m_IndexExpression; }
		const Token& GetCloseBracketToken() const { return m_CloseBracketToken; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::ArrayIndex; }

		friend struct PrattParser;

	private:
		Ref<AstNode> m_ArrayExpression;
		Token m_OpenBracketToken;
		Ref<AstNode> m_IndexExpression;
		Token m_CloseBracketToken;
		mutable Ref<AstNode> m_Children[2] = { nullptr, nullptr };
	};
}
