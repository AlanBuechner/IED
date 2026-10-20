#include "Backend/HIR/PackedViews.h"
#include "Model/Types/SwizzleType.h"

namespace Scripting
{
	namespace
	{
		using Region = ObjectType::PackedViewRegion;

		// The packed region a name's lanes belong to: a pure vector's only region, else the one holding its
		// first letter.
		const Region* RegionOf(const ObjectType* obj, const Core::String& name)
		{
			if (obj->IsPurePackedVector()) return obj->GetPackedVectorRegion();
			return name.empty() ? nullptr : obj->FindPackedLane(Core::String(1, name[0]));
		}

		// Where a region's vector lives: a pure vector is its own region; any other object stores the region in
		// a field of its own (BUG-184).
		const LValue* RegionStorage(LoweringContext& ctx, const ObjectType* obj, const Region& region, const LValue* base)
		{
			if (obj->IsPurePackedVector()) return base;
			const ObjectType::FieldInfo* storage = obj->PackedStorage(region);
			if (!storage) return base;
			LValue lv; lv.kind = LValue::Kind::Field; lv.type = storage->m_Type; lv.field = storage; lv.base = base;
			return ctx.NewLValue(lv);
		}

		const LValue* WholeVector(LoweringContext& ctx, const SwizzleType* region, const LValue* storage)
		{
			LValue lv; lv.kind = LValue::Kind::Swizzle; lv.base = storage;
			lv.type = ctx.queries().TypeStore()->Swizzle(region->GetElementType(), region->GetLaneCount());
			for (uint32 i = 0; i < region->GetLaneCount(); ++i) lv.laneIndices.push_back(i);
			return ctx.NewLValue(lv);
		}

		const LValue* OneLane(LoweringContext& ctx, const SwizzleType* region, const LValue* storage, const Core::String& name)
		{
			const int index = region->GetLaneIndex(name);
			if (index < 0) return nullptr;
			LValue lv; lv.kind = LValue::Kind::Lane; lv.type = region->GetElementType();
			lv.base = storage; lv.laneIndex = static_cast<uint32>(index);
			return ctx.NewLValue(lv);
		}

		// `xz`, `zyx`: a sub-vector of the named lanes, read as one value or written lane by lane.
		const LValue* LaneRun(LoweringContext& ctx, const SwizzleType* region, const LValue* storage, const Core::String& name)
		{
			std::vector<uint32> indices;
			for (char c : std::string(name.c_str()))
			{
				const int index = region->GetLaneIndex(Core::String(1, c));
				if (index < 0) return nullptr;
				indices.push_back(static_cast<uint32>(index));
			}
			if (indices.size() < 2) return nullptr;
			LValue lv; lv.kind = LValue::Kind::Swizzle; lv.base = storage;
			lv.type = ctx.queries().TypeStore()->Swizzle(region->GetElementType(), static_cast<uint32>(indices.size()));
			lv.laneIndices = std::move(indices);
			lv.isUnpacked = !region->IsPacked(); // an unpacked run stores field by field, not with a shufflevector
			return ctx.NewLValue(lv);
		}
	}

	const LValue* PackedViewLValue(LoweringContext& ctx, const ObjectType* obj, const LValue* base, const Core::String& name)
	{
		if (!obj) return nullptr;
		if (const Region* aliased = obj->FindPackedArrayAlias(name); aliased && aliased->m_Swizzle)
			return WholeVector(ctx, aliased->m_Swizzle, RegionStorage(ctx, obj, *aliased, base));
		const Region* region = RegionOf(obj, name);
		if (!region || !region->m_Swizzle || !region->m_Swizzle->GetElementType()) return nullptr;
		const LValue* storage = RegionStorage(ctx, obj, *region, base);
		return name.size() == 1 ? OneLane(ctx, region->m_Swizzle, storage, name) : LaneRun(ctx, region->m_Swizzle, storage, name);
	}
} // namespace Scripting
