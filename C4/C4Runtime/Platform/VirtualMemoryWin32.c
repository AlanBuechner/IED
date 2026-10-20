// Virtual memory for Bedrock's Platform namespace (PlatformRuntimeC.h) over VirtualAlloc, VirtualFree,
// VirtualProtect and VirtualQuery.

#include "PlatformRuntimeC.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>

// An aligned reservation is found by reserving more, releasing it and reserving the aligned part; another thread
// may take that range in between, so it retries.
enum { kAlignedReserveAttempts = 16 };

// Modifiers VirtualQuery reports beside the rights (PAGE_GUARD, PAGE_NOCACHE, PAGE_WRITECOMBINE, CFG targets).
enum { kRightsMask = 0xFF };

static DWORD ToPageRights(int32_t protection)
{
	switch (protection)
	{
	case kProtectNone: return PAGE_NOACCESS;
	case kProtectRead: return PAGE_READONLY;
	case kProtectReadWrite: return PAGE_READWRITE;
	case kProtectExecute: return PAGE_EXECUTE;
	case kProtectReadExecute: return PAGE_EXECUTE_READ;
	case kProtectReadWriteExecute: return PAGE_EXECUTE_READWRITE;
	default: return 0;
	}
}

// Copy-on-write pages (an image's data) read as writable: a write gives the process its own copy.
static int32_t FromPageRights(DWORD rights)
{
	switch (rights & kRightsMask)
	{
	case PAGE_READONLY: return kProtectRead;
	case PAGE_READWRITE:
	case PAGE_WRITECOPY: return kProtectReadWrite;
	case PAGE_EXECUTE: return kProtectExecute;
	case PAGE_EXECUTE_READ: return kProtectReadExecute;
	case PAGE_EXECUTE_READWRITE:
	case PAGE_EXECUTE_WRITECOPY: return kProtectReadWriteExecute;
	default: return kProtectNone;
	}
}

static int32_t FromState(DWORD state)
{
	if (state == MEM_COMMIT) return kRegionCommitted;
	return state == MEM_RESERVE ? kRegionReserved : kRegionFree;
}

static bool IsPowerOfTwo(int64_t value) { return value > 0 && (value & (value - 1)) == 0; }

static SYSTEM_INFO SystemInfo(void)
{
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info;
}

int64_t c4_vm_page_size(void) { return (int64_t)SystemInfo().dwPageSize; }

int64_t c4_vm_allocation_granularity(void) { return (int64_t)SystemInfo().dwAllocationGranularity; }

static void* ReserveAligned(int64_t size, int64_t alignment)
{
	if (size > INT64_MAX - alignment) return NULL;
	for (int attempt = 0; attempt < kAlignedReserveAttempts; ++attempt)
	{
		void* probe = VirtualAlloc(NULL, (SIZE_T)(size + alignment), MEM_RESERVE, PAGE_NOACCESS);
		if (!probe) return NULL;
		const uintptr_t aligned = ((uintptr_t)probe + (uintptr_t)alignment - 1) & ~(uintptr_t)(alignment - 1);
		VirtualFree(probe, 0, MEM_RELEASE);
		void* reserved = VirtualAlloc((void*)aligned, (SIZE_T)size, MEM_RESERVE, PAGE_NOACCESS);
		if (reserved) return reserved;
	}
	return NULL;
}

void* c4_vm_reserve(int64_t size, int64_t alignment)
{
	if (size <= 0 || alignment < 0 || (alignment != 0 && !IsPowerOfTwo(alignment))) return NULL;
	if (alignment > c4_vm_allocation_granularity()) return ReserveAligned(size, alignment);
	return VirtualAlloc(NULL, (SIZE_T)size, MEM_RESERVE, PAGE_NOACCESS);
}

void* c4_vm_allocate(int64_t size, int32_t protection)
{
	const DWORD rights = ToPageRights(protection);
	if (size <= 0 || !rights) return NULL;
	return VirtualAlloc(NULL, (SIZE_T)size, MEM_RESERVE | MEM_COMMIT, rights);
}

int32_t c4_vm_commit(void* address, int64_t size, int32_t protection)
{
	const DWORD rights = ToPageRights(protection);
	if (!address || size <= 0 || !rights) return -1;
	return VirtualAlloc(address, (SIZE_T)size, MEM_COMMIT, rights) ? 0 : -1;
}

int32_t c4_vm_decommit(void* address, int64_t size)
{
	if (!address || size <= 0) return -1;
	return VirtualFree(address, (SIZE_T)size, MEM_DECOMMIT) ? 0 : -1;
}

int32_t c4_vm_release(void* base)
{
	if (!base) return -1;
	return VirtualFree(base, 0, MEM_RELEASE) ? 0 : -1;
}

int32_t c4_vm_protect(void* address, int64_t size, int32_t protection)
{
	const DWORD rights = ToPageRights(protection);
	DWORD previous = 0;
	if (!address || size <= 0 || !rights) return -1;
	return VirtualProtect(address, (SIZE_T)size, rights, &previous) ? 0 : -1;
}

int32_t c4_vm_query(const void* address, c4_memory_region* region)
{
	MEMORY_BASIC_INFORMATION info;
	if (!region || VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) return -1;
	region->base = info.BaseAddress;
	region->size = (int64_t)info.RegionSize;
	region->state = FromState(info.State);
	region->allocationBase = region->state == kRegionFree ? NULL : info.AllocationBase;
	region->protection = region->state == kRegionCommitted ? FromPageRights(info.Protect) : kProtectNone;
	return 0;
}

void c4_vm_flush_instruction_cache(const void* address, int64_t size)
{
	if (address && size > 0) FlushInstructionCache(GetCurrentProcess(), address, (SIZE_T)size);
}
