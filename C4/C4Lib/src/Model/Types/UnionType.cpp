#include "Model/Types/UnionType.h"
#include "Model/Types/UnionLayout.h"

namespace Scripting
{
	UnionType::UnionType(const Core::String& name)
		: Type(TypeKind::Union), m_FullName(name)
	{
	}

	Core::String UnionType::GetName() const
	{
		return m_FullName;
	}

	void UnionType::CalculateLayout()
	{
		std::vector<ObjectType::FieldInfo*> members;
		for (auto& field : m_Fields) members.push_back(&field);
		const UnionLayout::Geometry geometry = UnionLayout::LayOut(members);
		for (auto& field : m_Fields) field.m_Offset = field.m_UnionIntraOffset;
		m_Alignment = geometry.align;
		m_Size = geometry.size;
	}

	int64 UnionType::GetFieldOffset(const Core::String& name) const
	{
		for (const auto& field : m_Fields) {
			if (field.mName == name) {
				return static_cast<int64>(field.m_Offset);
			}
		}
		return -1;
	}
} // namespace Scripting
