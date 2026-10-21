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

	// C4's spelling: a leading const is the base type's, a const before a `*` makes that pointer const - so a pointer
	// to a const pointer to Box is `Box const**`, and every layer's const shows (BUG-321).
	Core::String PointerType::ToString() const
	{
		if (!m_PointeeType) return "void*";
		const auto* inner = dynamic_cast<const PointerType*>(m_PointeeType);
		if (!inner || !m_ConstReferent) return Core::StringFormat::Format("{0}{1}*", m_ConstReferent ? "const " : "", m_PointeeType->ToString());
		Core::String pointee = inner->ToString();
		const size_t star = pointee.rfind('*');
		return pointee.substr(0, star) + " const" + pointee.substr(star) + "*";
	}

	Core::String PointerType::GetName() const
	{
		return ToString();
	}
} // namespace Scripting
