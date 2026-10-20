#include "Model/Types/SwizzleType.h"

namespace Scripting
{
	SwizzleType::SwizzleType(const Core::String& name, const Type* elementType)
		: Type(TypeKind::Swizzle), m_FullName(name), m_ElementType(elementType)
	{
		if (elementType && elementType->IsValid()) {
			m_Alignment = elementType->GetAlignment();
		}
	}

	void SwizzleType::AddLane(const Core::String& laneName, bool packed)
	{
		AddLaneAt(laneName, packed, static_cast<uint32>(m_Lanes.Count()));
	}

	void SwizzleType::AddLaneAt(const Core::String& laneName, bool packed, uint32 index)
	{
		LaneInfo lane;
		lane.m_Name = laneName;
		lane.m_Index = index;
		lane.m_Packed = packed;
		m_Lanes.Push(lane);
	}

	uint32 SwizzleType::GetLaneCount() const
	{
		uint32 count = 0;
		for (const auto& lane : m_Lanes)
			if (lane.m_Index + 1 > count) count = lane.m_Index + 1;
		return count > m_ExplicitLaneCount ? count : m_ExplicitLaneCount;
	}

	// The largest power of two dividing the vector's size: an array of vectors then has no padding and every
	// element is aligned (Vec4: 16, Vec2: 8, Vec3: 4). A power-of-two size is LLVM's own alignment for <N x T>.
	static size_t PackedSimdAlignment(size_t vecSize)
	{
		return vecSize & (~vecSize + 1);
	}

	void SwizzleType::CalculateLayout()
	{
		if (!m_ElementType || !m_ElementType->IsValid()) return;
		size_t elemSize = m_ElementType->GetSize();
		// GetLaneCount() = max named index + 1, NOT the entry count: aliased views name the SAME lane
		// twice (x and r both index 0), so m_Lanes.Count() would overstate the vector width.
		size_t laneCount = GetLaneCount();

		if (IsPacked())
		{
			// packed lowers to an LLVM <N x T> SIMD value in exactly N * sizeof(T) bytes. Codegen stores a
			// vector whose size is not a power of two as [N x T] inside aggregates and gives its accesses this
			// alignment (VectorAlignment), so LLVM's layout and C4's agree.
			m_Size = elemSize * laneCount;
			m_Alignment = PackedSimdAlignment(m_Size);
			return;
		}

		// unpacked: a struct of N identical T fields (no SIMD register).
		m_Size = elemSize * laneCount;
		if (m_Size % m_Alignment != 0 && m_Alignment > 1)
			m_Size = (m_Size + m_Alignment - 1) & ~(m_Alignment - 1);
	}

	int SwizzleType::GetLaneIndex(const Core::String& name) const
	{
		for (const auto& lane : m_Lanes) {
			if (lane.m_Name == name) return static_cast<int>(lane.m_Index);
		}
		return -1;
	}

	Core::String SwizzleType::GetName() const
	{
		return m_FullName;
	}

	Core::String SwizzleType::WhyNotAVector(const Type* element, uint64 lanes, uint64 minimumLanes)
	{
		const Core::String shape = "packed " + (element ? element->GetName() : Core::String("?")) + "[" + std::to_string(lanes) + "]";
		if (!element || !element->IsNumeric() || element->GetKind() == TypeKind::Enum)
			return "'" + shape + "' is not a vector: the element of a packed vector is a number";
		if (lanes < minimumLanes || lanes > kMaxLanes)
			return "'" + shape + "' is not a vector: a packed vector has " + std::to_string(minimumLanes) + " to 16 lanes";
		if (lanes * element->GetSize() > kMaxBytes)
			return "'" + shape + "' is " + std::to_string(lanes * element->GetSize()) + " bytes: a packed vector holds at most 64 (16 floats)";
		return "";
	}
} // namespace Scripting
