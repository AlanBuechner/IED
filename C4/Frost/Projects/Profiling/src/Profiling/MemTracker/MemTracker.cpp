#include "MemTracker.h"

Core::MemStats Core::MemStats::s_MemStats;

#if ENABLE_MEMORY_TRACKER
#if !defined(HEADER_TOOL)

#include "Profiling/Itp.h"

#include <new>

inline void* new_internal(size_t size)
{
	void* ptr = malloc(size);
	if (ptr)
	{
		Core::GetMemStats().numAllocations++;
		Core::GetMemStats().totalAllocations++;
		Core::GetMemStats().allocatedBytes += size;

#if ENABLE_PROFILING
		itp_alloc(ptr, size, 0);
#endif

		return ptr;
	}

	throw std::bad_alloc();
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
		itp_free(ptr, 0);
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

void operator delete(void* ptr) noexcept
{
	delete_internal(ptr);
}

void operator delete[](void* ptr) noexcept
{
	delete_internal(ptr);
}

#endif
#endif
