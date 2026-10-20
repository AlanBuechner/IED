#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"

namespace Scripting
{
	/// Type check expression: operand is Type.
	struct TypeCheckNode : public AstNode
	{
		TypeCheckNode() = default;

		const Ref<AstNode>& GetOperand() const { return m_Operand; }
		const Token& GetIsToken() const { return m_IsToken; }
		const Ref<QualTypeNode>& GetTargetType() const { return m_TargetType; }

		std::span<const Ref<AstNode>> Children() const override;
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::TypeCheck; }

		friend struct PrattParser;

	private:
		Ref<AstNode> m_Operand;
		Token m_IsToken;
		Ref<QualTypeNode> m_TargetType;
		mutable Core::Vector<Ref<AstNode>> m_Children;
	};
}
