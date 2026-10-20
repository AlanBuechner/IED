#include "Model/Types/ReferenceType.h"
#include "Core/Core.h"

namespace Scripting
{
	ReferenceType::ReferenceType(const Type* referentType, bool constReferent)
		: Type(TypeKind::Reference), m_ReferentType(referentType), m_ConstReferent(constReferent)
	{
		m_Size = sizeof(void*);
		m_Alignment = sizeof(void*);
	}

	Core::String ReferenceType::ToString() const
	{
		if (!m_ReferentType) return "void&";
		return Core::StringFormat::Format("{0}{1}&", m_ConstReferent ? "const " : "", m_ReferentType->ToString());
	}

	Core::String ReferenceType::GetName() const
	{
		return ToString();
	}
} // namespace Scripting
