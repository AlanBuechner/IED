#include "Model/Types/AliasType.h"

namespace Scripting
{
	AliasType::AliasType(const Core::String& name, const Type* aliasedType)
		: Type(TypeKind::Alias), m_FullName(name), m_AliasedType(aliasedType)
	{
		if (aliasedType) {
			m_Size = aliasedType->GetSize();
			m_Alignment = aliasedType->GetAlignment();
		}
	}

	Core::String AliasType::GetName() const
	{
		return m_FullName;
	}
} // namespace Scripting
