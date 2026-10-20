#include "Model/Types/UnionLayout.h"
#include <unordered_map>

namespace Scripting::UnionLayout
{
	namespace
	{
		size_t AlignUp(size_t value, size_t align) { return align > 1 ? (value + align - 1) & ~(align - 1) : value; }

		bool Laid(const ObjectType::FieldInfo* member) { return member && !member->m_IsStatic && member->m_Type && member->m_Type->IsValid(); }
	}

	Geometry LayOut(const std::vector<ObjectType::FieldInfo*>& members)
	{
		Geometry geometry;
		std::unordered_map<int, size_t> viewEnds;
		for (ObjectType::FieldInfo* member : members)
		{
			if (!Laid(member)) continue;
			const Type* type = member->m_Type;
			if (type->GetAlignment() > geometry.align) geometry.align = type->GetAlignment();
			size_t start = 0;
			if (member->m_UnionViewId >= 0)
			{
				size_t& end = viewEnds[member->m_UnionViewId];
				start = AlignUp(end, type->GetAlignment());
				end = start + type->GetSize();
			}
			member->m_UnionIntraOffset = start;
			if (start + type->GetSize() > geometry.size) geometry.size = start + type->GetSize();
		}
		geometry.size = AlignUp(geometry.size, geometry.align);
		return geometry;
	}

	std::vector<const ObjectType::FieldInfo*> RegionMembers(const Core::Vector<ObjectType::FieldInfo>& fields, int region)
	{
		std::vector<const ObjectType::FieldInfo*> members;
		for (const auto& field : fields)
			if (!field.m_IsStatic && field.m_UnionRegionId == region) members.push_back(&field);
		return members;
	}

	bool IsPrimary(const std::vector<const ObjectType::FieldInfo*>& members, const ObjectType::FieldInfo& member)
	{
		for (const ObjectType::FieldInfo* first : members)
		{
			if (!Laid(first)) continue;
			if (first == &member) return true;
			return first->m_UnionViewId >= 0 && first->m_UnionViewId == member.m_UnionViewId;
		}
		return false;
	}
}
