#include "CastExpressionNode.h"
#include "../Declarations/QualTypeNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	void CastExpressionNode::SetOperand(Ref<AstNode> operand)
	{
		m_Operand = operand;
		KeepChildren();
	}

	void CastExpressionNode::SetTargetType(Ref<QualTypeNode> type)
	{
		m_TargetType = type;
		KeepChildren();
	}

	void CastExpressionNode::KeepChildren()
	{
		m_Children.Clear();
		if (m_Operand) m_Children.Push(m_Operand);
		if (m_TargetType) m_Children.Push(m_TargetType);
	}

	Core::String CastExpressionNode::ToString() const
	{
		Core::String kindName;
		switch (m_Kind)
		{
			case CastKind::StaticCast:      kindName = "static_cast"; break;
			case CastKind::DynamicCast:     kindName = "dynamic_cast"; break;
			case CastKind::ReinterpretCast: kindName = "reinterpret_cast"; break;
			case CastKind::ConstCast:       kindName = "const_cast"; break;
			case CastKind::CStyle:          kindName = ""; break;
		}

		if (m_Kind == CastKind::CStyle)
			return Core::StringFormat::Format("({0}){1}",
				m_TargetType ? m_TargetType->ToString() : "",
				m_Operand ? m_Operand->ToString() : "");

		return Core::StringFormat::Format("{0}<{1}>({2})",
			kindName,
			m_TargetType ? m_TargetType->ToString() : "",
			m_Operand ? m_Operand->ToString() : "");
	}

}
