#include "TernaryExpressionNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> TernaryExpressionNode::Children() const
	{
		m_Children[0] = m_Condition;
		m_Children[1] = m_ThenBranch;
		m_Children[2] = m_ElseBranch;
		int count = 0;
		if (m_Condition) count++;
		if (m_ThenBranch) count++;
		if (m_ElseBranch) count++;
		// Return contiguous span only if all are present and contiguous
		if (count == 3) return { m_Children, 3 };
		return {};
	}

	Core::String TernaryExpressionNode::ToString() const
	{
		return Core::StringFormat::Format("{0} ? {1} : {2}",
			m_Condition ? m_Condition->ToString() : "",
			m_ThenBranch ? m_ThenBranch->ToString() : "",
			m_ElseBranch ? m_ElseBranch->ToString() : "");
	}

}
