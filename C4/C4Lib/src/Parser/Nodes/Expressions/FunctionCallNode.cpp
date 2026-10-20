#include "FunctionCallNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> FunctionCallNode::Children() const
	{
		// Children = callee + arguments (all in one contiguous span)
		m_Children.Clear();
		if (m_Callee) m_Children.Push(m_Callee);
		for (auto& arg : m_Arguments)
			if (arg) m_Children.Push(arg);
		return m_Children;
	}

	Core::String FunctionCallNode::ToString() const
	{
		Core::String result = Core::StringFormat::Format("{0}(", m_Callee ? m_Callee->ToString() : "");
		for (uint32 i = 0; i < m_Arguments.Count(); ++i)
		{
			if (i > 0) result += ", ";
			if (m_Arguments[i])
				result += m_Arguments[i]->ToString();
		}
		result += ")";
		return result;
	}

}
