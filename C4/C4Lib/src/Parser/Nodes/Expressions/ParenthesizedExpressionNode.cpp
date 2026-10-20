#include "ParenthesizedExpressionNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> ParenthesizedExpressionNode::Children() const
	{
		if (m_Expression)
			return std::span<const Ref<AstNode>>(&const_cast<Ref<AstNode>&>(m_Expression), 1);
		return {};
	}

	Core::String ParenthesizedExpressionNode::ToString() const
	{
		return Core::StringFormat::Format("({0})", m_Expression ? m_Expression->ToString() : "");
	}

}
