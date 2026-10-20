#include "BinaryExpressionNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> BinaryExpressionNode::Children() const
	{
		m_Children[0] = m_LeftOperand;
		m_Children[1] = m_RightOperand;
		if (m_LeftOperand && m_RightOperand)
			return { m_Children, 2 };
		if (m_LeftOperand)
			return { m_Children, 1 };
		return {};
	}

	Core::String BinaryExpressionNode::ToString() const
	{
		return Core::StringFormat::Format("{0} {1} {2}",
			m_LeftOperand ? m_LeftOperand->ToString() : "",
			m_OperatorToken.GetText(),
			m_RightOperand ? m_RightOperand->ToString() : "");
	}

}
