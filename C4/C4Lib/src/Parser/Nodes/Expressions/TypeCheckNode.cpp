#include "TypeCheckNode.h"
#include "../Declarations/QualTypeNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	void TypeCheckNode::SetOperand(Ref<AstNode> operand)
	{
		m_Operand = operand;
		KeepChildren();
	}

	void TypeCheckNode::SetTargetType(Ref<QualTypeNode> type)
	{
		m_TargetType = type;
		KeepChildren();
	}

	void TypeCheckNode::KeepChildren()
	{
		m_Children.Clear();
		if (m_Operand) m_Children.Push(m_Operand);
		if (m_TargetType) m_Children.Push(m_TargetType);
	}

	Core::String TypeCheckNode::ToString() const
	{
		return Core::StringFormat::Format("{0} is {1}",
			m_Operand ? m_Operand->ToString() : "",
			m_TargetType ? m_TargetType->ToString() : "");
	}

}
