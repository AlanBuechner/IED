#include "SafeCastNode.h"
#include "../Declarations/QualTypeNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> SafeCastNode::Children() const
	{
		m_Children.Clear();
		if (m_Operand) m_Children.Push(m_Operand);
		if (m_TargetType) m_Children.Push(m_TargetType);
		return m_Children;
	}

	Core::String SafeCastNode::ToString() const
	{
		return Core::StringFormat::Format("{0} as {1}",
			m_Operand ? m_Operand->ToString() : "",
			m_TargetType ? m_TargetType->ToString() : "");
	}

}
