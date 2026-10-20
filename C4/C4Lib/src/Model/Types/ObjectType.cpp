#include "Model/Types/ObjectType.h"
#include "Model/Types/UnionLayout.h"
#include "Model/Types/SwizzleType.h" // PackedViewRegion's SIMD storage (GetSize/GetAlignment in CalculateLayout)
#include "Model/Types/ClassHierarchy.h" // inherited field offsets
#include "Parser/AstNode.h"           // a packed region's declaration position

namespace Scripting
{
	ObjectType::ObjectType(TypeKind kind, const Core::String& name)
		: Type(kind), m_FullName(name)
	{
	}

	Core::String ObjectType::GetName() const
	{
		return m_FullName;
	}

	const ObjectType::FieldInfo* ObjectType::GetFieldByName(const Core::String& name) const
	{
		for (const auto& field : m_Fields) {
			if (field.mName == name) {
				return &field;
			}
		}
		for (const ObjectType* base : m_BaseTypes) {
			if (!base || !base->IsValid()) continue;
			const FieldInfo* found = base->GetFieldByName(name);
			if (found) return found;
		}
		return nullptr;
	}

	const ObjectType::MethodInfo* ObjectType::FindMethodByName(const Core::String& name) const
	{
		for (const auto& method : m_Methods) {
			if (method.mName == name) {
				return &method;
			}
		}
		for (const ObjectType* base : m_BaseTypes) {
			if (!base || !base->IsValid()) continue;
			const MethodInfo* found = base->FindMethodByName(name);
			if (found) return found;
		}
		for (const auto& method : m_ExtensionMethods) {
			if (method.mName == name) {
				return &method;
			}
		}
		return nullptr;
	}

	int64 ObjectType::GetFieldOffset(const Core::String& name) const
	{
		ClassHierarchy::FieldHit hit = ClassHierarchy::FindField(this, name);
		if (!hit.field) return -1;
		return hit.ownerOffset + static_cast<int64>(hit.field->m_Offset);
	}

	const ObjectType::PackedViewRegion* ObjectType::FindPackedLane(const Core::String& laneName) const
	{
		for (const auto& r : m_PackedViews)
			if (r.m_Swizzle && r.m_Swizzle->GetLaneIndex(laneName) >= 0) return &r;
		return nullptr;
	}

	void ObjectType::AddPackedRegion(const SwizzleType* region, const AstNode* declared)
	{
		FieldInfo storage;
		storage.mName = "__packed" + std::to_string(m_PackedViews.Count());
		storage.m_Type = region;
		storage.m_IsPackedStorage = true;
		storage.m_DeclaringNode = declared;
		uint32 index = 0;
		for (uint32 i = 0; i < m_Fields.Count(); ++i)
		{
			const AstNode* node = m_Fields[i].m_DeclaringNode;
			if (node && declared && node->startLocation.byteOffset < declared->startLocation.byteOffset) index = i + 1;
		}
		m_Fields.Insert(index, storage);
		m_LaidOut = false;
		AddPackedView(region, storage.mName);
	}

	// Each anonymous-union region's geometry, before the fields are placed. Done at layout (not in pass 2) so a
	// member of a forward object type recomputes when FinalizeLayouts runs layout to a fixed point.
	void ObjectType::LayOutUnionRegions()
	{
		int regionCount = 0;
		for (const auto& f : m_Fields) if (f.m_InUnionRegion && f.m_UnionRegionId >= regionCount) regionCount = f.m_UnionRegionId + 1;
		for (int rid = 0; rid < regionCount; ++rid)
		{
			std::vector<FieldInfo*> members;
			for (auto& f : m_Fields)
				if (f.m_InUnionRegion && !f.m_IsStatic && f.m_UnionRegionId == rid) members.push_back(&f);
			if (members.empty()) continue;
			const UnionLayout::Geometry geometry = UnionLayout::LayOut(members);
			for (FieldInfo* f : members)
			{
				f->m_UnionRegionAlign = geometry.align;
				f->m_UnionRegionSize = geometry.size;
				f->m_UnionRegionFirst = f == members.front();
				f->m_UnionRegionLast = f == members.back();
			}
		}
	}

	const ObjectType::FieldInfo* ObjectType::PackedStorage(const PackedViewRegion& region) const
	{
		for (const auto& f : m_Fields)
			if (f.m_IsPackedStorage && f.mName == region.m_FieldName) return &f;
		return nullptr;
	}

	const ObjectType::PackedViewRegion* ObjectType::FindPackedArrayAlias(const Core::String& name) const
	{
		for (const auto& r : m_PackedViews)
			if (r.m_Swizzle && !r.m_Swizzle->GetArrayAlias().empty() && r.m_Swizzle->GetArrayAlias() == name) return &r;
		return nullptr;
	}

	bool ObjectType::IsPurePackedVector() const
	{
		if (!m_IsDefined) return false;
		if (m_BaseTypes.Count() != 0) return false;         // no inheritance
		if (IsPolymorphic()) return false;                  // no vptr slot
		const PackedViewRegion* region = nullptr;
		for (const auto& r : m_PackedViews)
		{
			if (!r.m_Swizzle || !r.m_Swizzle->IsPacked()) return false;
			if (!region) region = &r;
			else return false; // more than one packed region -> not a single vector
		}
		if (!region) return false;
		for (const auto& f : m_Fields)
		{
			if (f.m_IsStatic) continue;
			if (!f.m_Type || !f.m_Type->IsValid()) continue;
			if (f.m_Type != region->m_Swizzle) return false; // a non-vector member -> not pure
		}
		return true;
	}

	// --- ObjectType layout (doc 02 pass-2 fixed point) ---

	bool ObjectType::IsPolymorphic() const
	{
		if (IsInterface()) return true;
		for (const auto& method : m_Methods)
			if (!method.m_IsStatic && (method.m_IsVirtual || method.m_IsOverride)) return true;
		for (const ObjectType* base : m_BaseTypes)
			if (base && base->IsPolymorphic()) return true;
		return false;
	}

	// Lazily compute this object's layout on demand (post-order over bases) so sizeof(T) is correct
	// as soon as T's own fields are resolved, not only after the end-of-pass-2 FinalizeLayouts sweep.
	void ObjectType::EnsureLayout()
	{
		if (m_LaidOut || m_LayingOut) return; // done, or a by-value cycle is on this stack
		if (!IsDefined()) return;             // still a shell / body not fully walked
		m_LayingOut = true;
		const_cast<ObjectType*>(this)->CalculateLayout();
		m_LayingOut = false;
		m_LaidOut = true;
	}

	size_t ObjectType::GetSize() const
	{
		const_cast<ObjectType*>(this)->EnsureLayout();
		return m_Size;
	}

	SourceLocation ObjectType::GetDeclLocation() const
	{
		return m_DeclNode ? m_DeclNode->startLocation : SourceLocation();
	}

	void ObjectType::CalculateLayout()
	{
		// A pure packed vector IS its SIMD region: its size is the vector's byte count (e.g. 8 for
		// <2 x f32>), NOT rounded up to the SIMD alignment. This matches the standalone `swizzle`
		// sizeof and is what lets it pass in one XMM register instead of being padded to a hidden-pointer sret.
		if (IsPurePackedVector())
		{
			const PackedViewRegion* region = GetPackedVectorRegion();
			m_Alignment = region->m_Swizzle->GetAlignment();
			m_Size = region->m_Swizzle->GetSize();
			return;
		}

		// Reset own-field offsets so repeated calls don't accumulate.
		for (auto& field : m_Fields) {
			field.m_Offset = 0;
		}

		// MSVC order: polymorphic bases first (the first is the primary base and shares offset 0's vptr), then
		// the type's own vptr at offset 0 when it is polymorphic without a polymorphic base, then the
		// non-polymorphic bases, then the fields. MSVC does not reuse a base's tail padding.
		size_t offset = 0;
		size_t maxAlign = 1;
		m_BaseOffsets.Clear();
		for (uint32 i = 0; i < m_BaseTypes.Count(); ++i) m_BaseOffsets.Push(0);
		m_PrimaryBase = nullptr;
		m_HasOwnVptr = false;
		auto placeBase = [&](uint32 i) {
			const ObjectType* baseType = m_BaseTypes[i];
			size_t baseAlign = baseType->GetAlignment();
			if (baseAlign > 1 && (offset % baseAlign) != 0) offset = (offset + baseAlign - 1) & ~(baseAlign - 1);
			if (baseAlign > maxAlign) maxAlign = baseAlign;
			m_BaseOffsets[i] = offset;
			offset += baseType->GetSize();
		};
		for (uint32 i = 0; i < m_BaseTypes.Count(); ++i)
		{
			const ObjectType* baseType = m_BaseTypes[i];
			if (!baseType || !baseType->IsValid() || !baseType->IsPolymorphic()) continue;
			if (!m_PrimaryBase) m_PrimaryBase = baseType;
			placeBase(i);
		}
		if (!m_PrimaryBase && IsPolymorphic())
		{
			m_HasOwnVptr = true;
			offset = sizeof(void*);
			if (alignof(void*) > maxAlign) maxAlign = alignof(void*);
		}
		for (uint32 i = 0; i < m_BaseTypes.Count(); ++i)
		{
			const ObjectType* baseType = m_BaseTypes[i];
			if (!baseType || !baseType->IsValid() || baseType->IsPolymorphic()) continue;
			placeBase(i);
		}

		LayOutUnionRegions();

		for (auto& field : m_Fields) {
			if (field.m_IsStatic) continue;
			const Type* fieldType = field.m_Type;
			if (!fieldType || !fieldType->IsValid()) continue;

			if (field.m_InUnionRegion) {
				// Anonymous-union region: all members share the region base offset plus their own
				// intra-region offset. Only the first member aligns, only the last advances - by the
				// whole padded region size.
				if (field.m_UnionRegionFirst) {
					size_t regionAlign = field.m_UnionRegionAlign;
					if (regionAlign > maxAlign) {
						maxAlign = regionAlign;
					}
					if (regionAlign > 1 && (offset % regionAlign) != 0) {
						offset = (offset + regionAlign - 1) & ~(regionAlign - 1);
					}
				}
				field.m_Offset = offset + field.m_UnionIntraOffset;
				if (field.m_UnionRegionLast) {
					offset += field.m_UnionRegionSize;
				}
				continue;
			}

			size_t fieldAlign = fieldType->GetAlignment();
			if (fieldAlign > maxAlign) {
				maxAlign = fieldAlign;
			}

			if (fieldAlign > 1 && (offset % fieldAlign) != 0) {
				offset = (offset + fieldAlign - 1) & ~(fieldAlign - 1);
			}
			field.m_Offset = offset;
			offset += fieldType->GetSize();
		}

		if (maxAlign > 1 && (offset % maxAlign) != 0) {
			offset = (offset + maxAlign - 1) & ~(maxAlign - 1);
		}

		m_Alignment = maxAlign;
		m_Size = offset;
	}

} // namespace Scripting

