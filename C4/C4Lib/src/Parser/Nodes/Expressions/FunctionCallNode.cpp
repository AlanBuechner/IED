#include "FunctionCallNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	void FunctionCallNode::SetCallee(Ref<AstNode> callee)
	{
		m_Callee = callee;
		m_Children.Clear();
		if (m_Callee) m_Children.Push(m_Callee);
		for (const Ref<AstNode>& argument : m_Arguments) m_Children.Push(argument);
	}

	void FunctionCallNode::AddArgument(Ref<AstNode> argument)
	{
		if (!argument) return;
		m_Arguments.Push(argument);
		m_Children.Push(argument);
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
