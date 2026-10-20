#include "Model/Types/AggregateSlots.h"
#include "Model/Types/SwizzleType.h"
#include "Model/Types/UnionLayout.h"
#include <set>

namespace Scripting::AggregateSlots
{
	namespace
	{
		Slot FieldSlot(const ObjectType::FieldInfo& field) { return { field.mName, field.m_Type, field.m_Offset, &field, -1 }; }

		Slot LaneSlot(const ObjectType::FieldInfo& storage, const Core::String& name, uint32 lane)
		{
			const Type* element = static_cast<const SwizzleType*>(storage.m_Type)->GetElementType();
			return { name, element, storage.m_Offset + lane * (element ? element->GetSize() : 0), &storage, (int32)lane };
		}

		// A packed region's lanes in index order, each by the first name a view gives it (`x`, not `r`); a region
		// whose only view is its lane array has unnamed lanes.
		void AddLanes(const ObjectType::FieldInfo& storage, std::vector<Slot>& slots)
		{
			const auto* region = static_cast<const SwizzleType*>(storage.m_Type);
			std::set<uint32> named;
			for (const SwizzleType::LaneInfo& lane : region->GetLanes())
				if (named.insert(lane.m_Index).second) slots.push_back(LaneSlot(storage, lane.m_Name, lane.m_Index));
			if (!named.empty()) return;
			for (uint32 i = 0; i < region->GetLaneCount(); ++i) slots.push_back(LaneSlot(storage, "", i));
		}
	}

	std::vector<Slot> Of(const ObjectType& type)
	{
		std::vector<Slot> slots;
		for (const auto& field : type.GetFields())
		{
			if (field.m_IsStatic || !field.m_Type) continue;
			if (field.m_IsPackedStorage) { AddLanes(field, slots); continue; }
			if (field.m_InUnionRegion && !UnionLayout::IsPrimary(UnionLayout::RegionMembers(type.GetFields(), field.m_UnionRegionId), field)) continue;
			slots.push_back(FieldSlot(field));
		}
		return slots;
	}

	bool Named(const ObjectType& type, const Core::String& name, Slot& out)
	{
		for (const auto& field : type.GetFields())
		{
			if (field.m_IsStatic || !field.m_Type) continue;
			if (field.m_IsPackedStorage)
			{
				const int lane = static_cast<const SwizzleType*>(field.m_Type)->GetLaneIndex(name);
				if (lane >= 0) { out = LaneSlot(field, name, (uint32)lane); return true; }
				continue;
			}
			if (field.mName == name) { out = FieldSlot(field); return true; }
		}
		return false;
	}
}
