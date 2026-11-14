#include "MemTracker.h"

Core::MemStats Core::MemStats::s_MemStats;

#if ENABLE_MEMORY_TRACKER
#if !defined(HEADER_TOOL)

#include "Profiling/Itp.h"

#include <new>
#include <malloc.h>

// The bytes the heap gave `ptr`: what it was asked for on Windows, at least that with glibc (counted alike both ways).
inline uint64 AllocationSize(void* ptr)
{
#if defined(_WIN32)
	return _msize(ptr);
#else
	return malloc_usable_size(ptr);
#endif
}

inline void* new_internal(size_t size)
{
	void* ptr = malloc(size);
	if (ptr)
	{
		Core::GetMemStats().numAllocations++;
		Core::GetMemStats().totalAllocations++;
		Core::GetMemStats().allocatedBytes += AllocationSize(ptr);

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
		uint64 size = AllocationSize(ptr);
		if (size > Core::GetMemStats().allocatedBytes)
			CORE_DEBUG_BREAK();

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
