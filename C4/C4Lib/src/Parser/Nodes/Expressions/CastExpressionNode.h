#pragma once
#include "Parser/AstNode.h"
#include "Core/Core.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"

namespace Scripting
{
	/// C++-style and C-style cast expressions.
	struct CastExpressionNode : public AstNode
	{
		enum class CastKind { StaticCast, DynamicCast, ReinterpretCast, ConstCast, CStyle };

		CastExpressionNode() : m_Kind(CastKind::StaticCast) {}

		const Ref<AstNode>& GetOperand() const { return m_Operand; }
		const Ref<QualTypeNode>& GetTargetType() const { return m_TargetType; }
		CastKind GetKind() const { return m_Kind; }
		const Token& GetCastKeywordToken() const { return m_CastKeywordToken; }

		/// The parser fills it through these, which keep Children (the operand, then the type) as they go: two threads
		/// walking one body read them, never rebuild them.
		void SetOperand(Ref<AstNode> operand);
		void SetTargetType(Ref<QualTypeNode> type);

		std::span<const Ref<AstNode>> Children() const override { return m_Children; }
		Core::String ToString() const override;
protected:
		NodeType Type() const override { return NodeType::CastExpression; }

		friend struct PrattParser;

	private:
		Ref<AstNode> m_Operand;
		Ref<QualTypeNode> m_TargetType;
		CastKind m_Kind;
		Token m_CastKeywordToken;
		Core::Vector<Ref<AstNode>> m_Children;

		void KeepChildren();
	};
}
