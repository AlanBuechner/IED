#include "ConditionalOrNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> ConditionalOrNode::Children() const
	{
		m_Children[0] = m_LeftOperand;
		m_Children[1] = m_RightOperand;
		if (m_LeftOperand && m_RightOperand)
			return { m_Children, 2 };
		if (m_LeftOperand)
			return { m_Children, 1 };
		return {};
	}

	Core::String ConditionalOrNode::ToString() const
	{
		return Core::StringFormat::Format("{0} || {1}",
			m_LeftOperand ? m_LeftOperand->ToString() : "",
			m_RightOperand ? m_RightOperand->ToString() : "");
	}

}
