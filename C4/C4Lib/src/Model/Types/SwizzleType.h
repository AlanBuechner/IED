#pragma once
#include "Model/Types/Type.h"

namespace Scripting
{
	/// Swizzle type (plan item 1 - language swizzling). A first-class SIMD vector with named
	/// single-character lanes. `packed` lowers to an LLVM <N x T> vector register; `unpacked` to a
	/// {T,...,T} struct. Lanes are accessed HLSL-style: v.x (scalar), v.xz (multi-lane read/write).
	class SwizzleType : public Type
	{
	public:
		struct LaneInfo
		{
			Core::String m_Name;   // single-character lane name
			uint32 m_Index = 0;    // position in the vector (declaration order)
			bool m_Packed = true;  // packed -> SIMD register; unpacked -> struct
		};

		SwizzleType(const Core::String& name, const Type* elementType);
		Core::String GetName() const override;
		Core::String ToString() const override { return GetName(); }

		const Core::String& GetFullName() const { return m_FullName; }
		void SetFullName(const Core::String& name) { m_FullName = name; }

		const Type* GetElementType() const { return m_ElementType; }
		void SetElementType(const Type* t) { m_ElementType = t; }
		// Number of vector lanes (max named index + 1). NOT m_Lanes.Count(): aliased views name the
		// SAME lane twice (x and r both index 0), so the entry count overstates the vector width.
		uint32 GetLaneCount() const;
		// Explicit vector width for a region whose lanes come from an array view (`packed T[N] data`)
		// rather than named lanes - GetLaneCount falls back to this when no lane is named.
		void SetVectorWidth(uint32 width) { m_ExplicitLaneCount = width; }
		// Optional whole-region array alias (`packed T[N] data` in this region): `data[i]` indexes the
		// SIMD vector by i. Empty when no view declared one.
		void SetArrayAlias(const Core::String& name) { m_ArrayAlias = name; }
		const Core::String& GetArrayAlias() const { return m_ArrayAlias; }
		const Core::Vector<LaneInfo>& GetLanes() const { return m_Lanes; }
		void AddLane(const Core::String& laneName, bool packed);
		// Add a lane at an explicit index (used by aliased views: x and r both map to index 0).
		void AddLaneAt(const Core::String& laneName, bool packed, uint32 index);
		int GetLaneIndex(const Core::String& name) const; // -1 if not a lane
		bool IsPacked() const { return m_RegionPacked || (m_Lanes.Count() > 0 && m_Lanes[0].m_Packed); }
		void SetRegionPacked(bool packed) { m_RegionPacked = packed; }

		void CalculateLayout();

		/// Why `lanes` lanes of `element` cannot be one SIMD vector (D-36): the element is a number, there are at
		/// least `minimumLanes` and at most 16 lanes, and the vector holds at most 64 bytes. Empty when they can.
		static Core::String WhyNotAVector(const Type* element, uint64 lanes, uint64 minimumLanes);
		static constexpr uint64 kMaxLanes = 16;
		static constexpr uint64 kMaxBytes = 64;

	private:
		Core::String m_FullName;
		const Type* m_ElementType = nullptr;
		Core::Vector<LaneInfo> m_Lanes;
		uint32 m_ExplicitLaneCount = 0; // vector width from an array view (named lanes take precedence)
		bool m_RegionPacked = false;    // the region's packed-ness, even when it has no named lanes
		Core::String m_ArrayAlias;      // whole-region array alias name (`data`), empty if none
	};
} // namespace Scripting
