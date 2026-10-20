#include "Model/Types/ArrayType.h"
#include "Core/Core.h"

namespace Scripting
{
	ArrayType::ArrayType(const Type* elementType, uint64 elementCount)
		: Type(TypeKind::Array), m_ElementType(elementType), m_ElementCount(elementCount)
	{
		if (elementType && elementType->IsValid()) {
			m_Size = elementType->GetSize() * elementCount;
			m_Alignment = elementType->GetAlignment();
		}
	}

	Core::String ArrayType::ToString() const
	{
		if (!m_ElementType) return "void[]";
		return Core::StringFormat::Format("{0}[{1}]", m_ElementType->ToString(), m_ElementCount);
	}

	Core::String ArrayType::GetName() const
	{
		return ToString();
	}
} // namespace Scripting
