#include "ConditionalAndNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> ConditionalAndNode::Children() const
	{
		if (m_Children[0] && m_Children[1]) return { m_Children, 2 };
		if (m_Children[0]) return { m_Children, 1 };
		return {};
	}

	Core::String ConditionalAndNode::ToString() const
	{
		return Core::StringFormat::Format("{0} && {1}",
			m_Children[0] ? m_Children[0]->ToString() : "",
			m_Children[1] ? m_Children[1]->ToString() : "");
	}

}
