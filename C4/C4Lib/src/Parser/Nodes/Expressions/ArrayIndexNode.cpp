#include "ArrayIndexNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> ArrayIndexNode::Children() const
	{
		m_Children[0] = m_ArrayExpression;
		m_Children[1] = m_IndexExpression;
		if (m_ArrayExpression && m_IndexExpression)
			return { m_Children, 2 };
		if (m_ArrayExpression)
			return { m_Children, 1 };
		return {};
	}

	Core::String ArrayIndexNode::ToString() const
	{
		return Core::StringFormat::Format("{0}[{1}]",
			m_ArrayExpression ? m_ArrayExpression->ToString() : "",
			m_IndexExpression ? m_IndexExpression->ToString() : "");
	}

}
