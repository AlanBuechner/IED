#include "TypeCheckNode.h"
#include "../Declarations/QualTypeNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> TypeCheckNode::Children() const
	{
		m_Children.Clear();
		if (m_Operand) m_Children.Push(m_Operand);
		if (m_TargetType) m_Children.Push(m_TargetType);
		return m_Children;
	}

	Core::String TypeCheckNode::ToString() const
	{
		return Core::StringFormat::Format("{0} is {1}",
			m_Operand ? m_Operand->ToString() : "",
			m_TargetType ? m_TargetType->ToString() : "");
	}

}
