#include "MemberAccessNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	bool MemberAccessNode::IsArrowAccess() const
	{
		return m_AccessOperatorToken.GetType() == TokenType::Arrow;
	}

	std::span<const Ref<AstNode>> MemberAccessNode::Children() const
	{
		if (m_BaseObject)
			return std::span<const Ref<AstNode>>(&const_cast<Ref<AstNode>&>(m_BaseObject), 1);
		return {};
	}

	Core::String MemberAccessNode::QualifierText() const
	{
		Core::String text;
		for (const Token& name : m_Qualifier) text += (text.empty() ? "" : "::") + name.GetText();
		return text;
	}

	Core::String MemberAccessNode::ToString() const
	{
		return Core::StringFormat::Format("{0}{1}{2}{3}",
			m_BaseObject ? m_BaseObject->ToString() : "",
			m_AccessOperatorToken.GetText(),
			IsQualified() ? QualifierText() + "::" : Core::String(),
			m_MemberNameToken.GetText());
	}

}
