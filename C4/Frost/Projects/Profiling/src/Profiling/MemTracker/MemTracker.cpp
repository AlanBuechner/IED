#include "MemTracker.h"

Core::MemStats Core::MemStats::s_MemStats;

#if ENABLE_MEMORY_TRACKER
#if !defined(HEADER_TOOL)

extern "C" void tracy_alloc(const void* ptr, size_t size);
extern "C" void tracy_free(const void* ptr);

inline void* new_internal(size_t size)
{
	void* ptr = malloc(size);
	if (ptr)
	{
		Core::GetMemStats().numAllocations++;
		Core::GetMemStats().totalAllocations++;
		Core::GetMemStats().allocatedBytes += size;

#if ENABLE_PROFILING
		tracy_alloc(ptr, size);
#endif

		return ptr;
	}

	return nullptr;
}

inline void delete_internal(void* ptr)
{
	if (ptr != nullptr)
	{
		uint64 size = _msize(ptr);
		if (size > Core::GetMemStats().allocatedBytes)
			__debugbreak();

		Core::GetMemStats().numAllocations--;
		Core::GetMemStats().totalFrees++;
		Core::GetMemStats().allocatedBytes -= size;

#if ENABLE_PROFILING
		tracy_free(ptr);
#endif

		free(ptr);
	}
}


void* operator new (size_t size)
{
	return new_internal(size);
}

void* operator new[] (size_t size)
{
	return new_internal(size);
}

void operator delete(void* ptr)
{
	delete_internal(ptr);
}

void operator delete[](void* ptr)
{
	delete_internal(ptr);
}

#endif
#endif
