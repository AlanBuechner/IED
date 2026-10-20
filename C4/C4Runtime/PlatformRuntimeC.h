#pragma once
// The operating-system services behind Bedrock's Platform namespace (D-63): virtual memory, system and process
// information, loaded libraries and time. One implementation file per area and OS (Platform/*Win32.c), so a port
// adds files and no caller changes. Text is UTF-8; a function that writes text into `out` returns its length
// without the terminator and writes it, null-terminated, only when `capacity` has room for both.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- virtual memory (Platform/VirtualMemoryWin32.c) ----------------------------------------------------
// Bedrock's Platform::Protection: the page rights x64 can express (write implies read).
enum
{
	kProtectNone = 0,
	kProtectRead = 1,
	kProtectReadWrite = 2,
	kProtectExecute = 3,
	kProtectReadExecute = 4,
	kProtectReadWriteExecute = 5
};

// Bedrock's Platform::RegionState.
enum { kRegionFree = 0, kRegionReserved = 1, kRegionCommitted = 2 };

typedef struct c4_memory_region
{
	void* base;             // the first page of the run of pages that share state and protection
	int64_t size;           // the run's bytes
	void* allocationBase;   // what the reservation's Reserve or Allocate returned (null when free)
	int32_t state;          // kRegion*
	int32_t protection;     // kProtect* (kProtectNone unless committed)
} c4_memory_region;

int64_t c4_vm_page_size(void);
// Reservations start at a multiple of this.
int64_t c4_vm_allocation_granularity(void);
// Address space only, no memory: `size` bytes rounded up to pages, starting at a multiple of `alignment` (a power
// of two; 0 for the allocation granularity). Null when size <= 0, the alignment is not a power of two, or the space
// is not there.
void* c4_vm_reserve(int64_t size, int64_t alignment);
// A reservation committed whole with `protection`. Null as c4_vm_reserve, or for an unknown protection.
void* c4_vm_allocate(int64_t size, int32_t protection);
// Backs [address, address + size), rounded out to pages, with zeroed memory (pages already committed keep their
// bytes) and gives every page `protection`. 0 on success, -1 when the range is not reserved, size <= 0, or the
// memory is not there.
int32_t c4_vm_commit(void* address, int64_t size, int32_t protection);
// Returns the pages' memory; the addresses stay reserved. 0 or -1.
int32_t c4_vm_decommit(void* address, int64_t size);
// Gives back a whole reservation; `base` must be what c4_vm_reserve or c4_vm_allocate returned. 0 or -1.
int32_t c4_vm_release(void* base);
// Changes the rights of committed pages. 0 or -1 (a page in the range is not committed, size <= 0).
int32_t c4_vm_protect(void* address, int64_t size, int32_t protection);
// The run of pages holding `address`. 0, or -1 when the address is outside the process's space.
int32_t c4_vm_query(const void* address, c4_memory_region* region);
// Makes code written to [address, address + size) visible to instruction fetch before it runs.
void c4_vm_flush_instruction_cache(const void* address, int64_t size);

// --- system and process (Platform/SystemWin32.c) ------------------------------------------------------
typedef struct c4_processor_info
{
	int32_t logicalProcessors;   // hardware threads the system runs
	int32_t cores;               // physical cores
	int32_t packages;            // processor sockets
	int32_t cacheLineBytes;      // the L1 data cache's line size (64 when the system does not say)
	int64_t l1DataBytes;         // one core's L1 data cache (0 when there is none)
	int64_t l2Bytes;             // one L2 cache
	int64_t l3Bytes;             // one L3 cache
} c4_processor_info;

typedef struct c4_memory_status
{
	int64_t totalPhysicalBytes;
	int64_t availablePhysicalBytes;
	int64_t commitLimitBytes;       // physical memory plus the page files
	int64_t availableCommitBytes;
	int64_t totalVirtualBytes;      // this process's user address space
	int64_t availableVirtualBytes;  // the part of it not reserved
} c4_memory_status;

typedef struct c4_process_memory
{
	int64_t workingSetBytes;        // this process's memory in RAM now
	int64_t peakWorkingSetBytes;
	int64_t privateBytes;           // memory committed to this process alone
	int64_t peakPrivateBytes;
} c4_process_memory;

void c4_system_processor_info(c4_processor_info* info);
void c4_system_memory_status(c4_memory_status* status);
void c4_system_process_memory(c4_process_memory* memory);
uint64_t c4_system_process_id(void);
// The running program's file. -1 when the system cannot say.
int64_t c4_system_executable_path(char* out, int64_t capacity);
// The variable's value; -1 when it is not set (or the name is empty, has an '=' or is not UTF-8).
int64_t c4_system_get_environment(const char* name, char* out, int64_t capacity);
// Sets the variable for this process and the processes it starts; a null `value` removes it. 0 or -1 (an empty
// name, one with an '=', text that is not UTF-8).
int32_t c4_system_set_environment(const char* name, const char* value);
// `count` bytes from the system's cryptographically secure generator. 0 or -1.
int32_t c4_system_random_bytes(void* out, int64_t count);

// --- libraries (Platform/LibraryWin32.c) --------------------------------------------------------------
// Loads a library (a DLL) and what it needs; null on failure, with the reason in `error` (truncated to fit). A path
// without a folder is searched for as the system searches; loading one twice gives the same module, counted.
void* c4_library_open(const char* path, char* error, int64_t errorCapacity);
// The address of an exported function or variable, or null.
void* c4_library_find(void* library, const char* symbol);
// Drops one count of the module; it unloads when the last goes.
void c4_library_close(void* library);

// --- time (Platform/TimeWin32.c) ----------------------------------------------------------------------
// The high-resolution counter (QueryPerformanceCounter): monotonic, `c4_time_tick_frequency` ticks per second.
int64_t c4_time_ticks(void);
int64_t c4_time_tick_frequency(void);
// The wall clock: nanoseconds since 1970-01-01 00:00:00 UTC, leap seconds not counted. It jumps when the system's
// time is set; measure durations with c4_clock_now_ns.
int64_t c4_time_unix_ns(void);
// Seconds to add to UTC for the local time at the instant `unixNs`, with daylight saving time as it was then.
int32_t c4_time_local_offset_seconds(int64_t unixNs);

#ifdef __cplusplus
}
#endif
