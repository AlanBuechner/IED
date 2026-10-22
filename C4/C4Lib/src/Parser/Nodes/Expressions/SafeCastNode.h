#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"

namespace Scripting
{
	/// Safe cast expression: operand as Type.
	struct SafeCastNode : public AstNode
	{
		SafeCastNode() = default;

		const Ref<AstNode>& GetOperand() const { return m_Operand; }
		const Token& GetAsToken() const { return m_AsToken; }
		const Ref<QualTypeNode>& GetTargetType() const { return m_TargetType; }

		/// The parser fills it through these, which keep Children (the operand, then the type) as they go: two threads
		/// walking one body read them, never rebuild them.
		void SetOperand(Ref<AstNode> operand);
		void SetTargetType(Ref<QualTypeNode> type);

		std::span<const Ref<AstNode>> Children() const override { return m_Children; }
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::SafeCast; }

		friend struct PrattParser;

	private:
		Ref<AstNode> m_Operand;
		Token m_AsToken;
		Ref<QualTypeNode> m_TargetType;
		Core::Vector<Ref<AstNode>> m_Children;

		void KeepChildren();
	};
}
