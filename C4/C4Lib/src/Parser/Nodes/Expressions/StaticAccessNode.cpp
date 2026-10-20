#include "StaticAccessNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> StaticAccessNode::Children() const
	{
		if (m_BaseType)
			return std::span<const Ref<AstNode>>(&const_cast<Ref<AstNode>&>(m_BaseType), 1);
		return {};
	}

	Core::String StaticAccessNode::ToString() const
	{
		return Core::StringFormat::Format("{0}{1}{2}",
			m_BaseType ? m_BaseType->ToString() : "",
			m_ScopeResolutionToken.GetText(),
			m_MemberNameToken.GetText());
	}

}
