#pragma once
#include "Types.h"
#include "Vector.h"
#include <mutex>
#include <bit>

namespace Core
{
	class DynamicBitAllocator
	{
	public:
		using BitType = uint64;
		static constexpr uint32 s_BitTypeSize = sizeof(BitType) * 8;

		uint32 GetNumPages() { return m_AllocatedSlots.Count(); }

		inline uint64 AllocateIndex();
		inline void Free(uint32 index) { ClearAllocatedBit(index); }

		inline bool HasAllocation(uint64 index) { return GetAllocatedBit(index); }
		
		inline void Clear();

	private:
		static constexpr uint64 Index(uint64 i) { return i / s_BitTypeSize; }
		static constexpr BitType Mask(uint64 i) { return ((BitType)1) << (i % s_BitTypeSize); }
		bool GetAllocatedBit(uint64 i) { return (m_AllocatedSlots[Index(i)] & Mask(i)) != 0; }
		void SetAllocatedBit(uint64 i) { m_AllocatedSlots[Index(i)] |= Mask(i); }
		void ClearAllocatedBit(uint64 i) { m_AllocatedSlots[Index(i)] &= ~Mask(i); }

	private:
		std::mutex m_Mutex;
		Core::Vector<BitType> m_AllocatedSlots{ 0 };
	};
}

namespace Core
{
	uint64 DynamicBitAllocator::AllocateIndex()
	{
		std::lock_guard g(m_Mutex);
		constexpr BitType MAX = ~0;
		for (uint32 i = 0; i < m_AllocatedSlots.Count(); i++) {
			if (m_AllocatedSlots[i] != MAX) {
				uint64 index = (i * s_BitTypeSize) + std::countr_one(m_AllocatedSlots[i]);
				SetAllocatedBit(index);
				return index;
			}
		}

		// could not find slot
		uint64 index = m_AllocatedSlots.Count() * s_BitTypeSize;
		m_AllocatedSlots.Push(0);
		SetAllocatedBit(index);
		return index;
	}

	void DynamicBitAllocator::Clear()
	{
		for (uint32 i = 0; i < m_AllocatedSlots.Count(); i++)
			m_AllocatedSlots[i] = 0;
	}

}