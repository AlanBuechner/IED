#pragma once
#include "Core/Types.h"
#include <atomic>

#if !defined(ENABLE_MEMORY_TRACKER)
#define ENABLE_MEMORY_TRACKER 0
#endif

namespace Core
{
	struct MemStats
	{
		std::atomic<uint64> totalAllocations = 0;
		std::atomic<uint64> totalFrees = 0;
		std::atomic<uint64> numAllocations = 0;
		std::atomic<uint64> allocatedBytes = 0;
		
#if ENABLE_MEMORY_TRACKER
		static constexpr bool IsActive = true;
#else
		static constexpr bool IsActive = false;
#endif

		static MemStats s_MemStats;
	};

	inline MemStats& GetMemStats() { return MemStats::s_MemStats; }
}


#if ENABLE_MEMORY_TRACKER
#if !defined(HEADER_TOOL)
#undef new
#undef delete

void* operator new (size_t size);
void* operator new[] (size_t size);

void operator delete(void* ptr);
void operator delete[](void* ptr);

#endif
#endif
