#include "Model/Types/EnumType.h"

namespace Scripting
{
	EnumType::EnumType(const Core::String& name, const Type* underlyingType)
		: Type(TypeKind::Enum), m_FullName(name), m_UnderlyingType(underlyingType)
	{
		if (underlyingType) {
			m_Size = underlyingType->GetSize();
			m_Alignment = underlyingType->GetAlignment();
		}
		else {
			m_Size = 4;
			m_Alignment = 4;
		}
	}

	Core::String EnumType::GetName() const
	{
		return m_FullName;
	}

	const EnumType::MemberInfo* EnumType::GetMemberByName(const Core::String& name) const
	{
		for (const auto& member : m_Members) {
			if (member.mName == name) {
				return &member;
			}
		}
		return nullptr;
	}
} // namespace Scripting
