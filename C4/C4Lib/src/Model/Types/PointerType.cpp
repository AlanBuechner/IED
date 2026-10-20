#include "Model/Types/PointerType.h"
#include "Core/Core.h"

namespace Scripting
{
	PointerType::PointerType(const Type* pointeeType, bool constReferent)
		: Type(TypeKind::Pointer), m_PointeeType(pointeeType), m_ConstReferent(constReferent)
	{
		m_Size = sizeof(void*);
		m_Alignment = sizeof(void*);
	}

	Core::String PointerType::ToString() const
	{
		if (!m_PointeeType) return "void*";
		return Core::StringFormat::Format("{0}{1}*", m_ConstReferent ? "const " : "", m_PointeeType->ToString());
	}

	Core::String PointerType::GetName() const
	{
		return ToString();
	}
} // namespace Scripting
