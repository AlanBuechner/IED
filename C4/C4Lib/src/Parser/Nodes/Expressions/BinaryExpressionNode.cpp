#include "BinaryExpressionNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> BinaryExpressionNode::Children() const
	{
		if (m_Children[0] && m_Children[1]) return { m_Children, 2 };
		if (m_Children[0]) return { m_Children, 1 };
		return {};
	}

	Core::String BinaryExpressionNode::ToString() const
	{
		return Core::StringFormat::Format("{0} {1} {2}",
			m_Children[0] ? m_Children[0]->ToString() : "",
			m_OperatorToken.GetText(),
			m_Children[1] ? m_Children[1]->ToString() : "");
	}

}
