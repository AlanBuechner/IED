#include "SafeCastNode.h"
#include "../Declarations/QualTypeNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	void SafeCastNode::SetOperand(Ref<AstNode> operand)
	{
		m_Operand = operand;
		KeepChildren();
	}

	void SafeCastNode::SetTargetType(Ref<QualTypeNode> type)
	{
		m_TargetType = type;
		KeepChildren();
	}

	void SafeCastNode::KeepChildren()
	{
		m_Children.Clear();
		if (m_Operand) m_Children.Push(m_Operand);
		if (m_TargetType) m_Children.Push(m_TargetType);
	}

	Core::String SafeCastNode::ToString() const
	{
		return Core::StringFormat::Format("{0} as {1}",
			m_Operand ? m_Operand->ToString() : "",
			m_TargetType ? m_TargetType->ToString() : "");
	}

}
