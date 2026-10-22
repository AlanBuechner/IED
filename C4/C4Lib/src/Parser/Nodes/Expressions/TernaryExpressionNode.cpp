#include "TernaryExpressionNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> TernaryExpressionNode::Children() const
	{
		if (m_Children[0] && m_Children[1] && m_Children[2]) return { m_Children, 3 };
		return {};
	}

	Core::String TernaryExpressionNode::ToString() const
	{
		return Core::StringFormat::Format("{0} ? {1} : {2}",
			m_Children[0] ? m_Children[0]->ToString() : "",
			m_Children[1] ? m_Children[1]->ToString() : "",
			m_Children[2] ? m_Children[2]->ToString() : "");
	}

}
