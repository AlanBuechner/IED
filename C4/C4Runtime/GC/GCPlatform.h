#pragma once
// The operating-system services the collector uses. GCPlatformWin32.c implements them; a port adds one file.

#include <stddef.h>
#include <stdint.h>

// `size` bytes of committed, zeroed memory aligned to `align` (a power of two), with its written pages tracked (D-79).
// NULL when the OS refuses.
void* gc_os_map(size_t size, size_t align);

// Written-page tracking over one mapping: forget what was written so far; or report every page written since the
// last report or forget, forgetting each as it is reported (a write while this runs is reported now or next time).
void gc_os_forget_writes(void* base, size_t size);
typedef void (*gc_page_fn)(void* ctx, uint8_t* page, size_t size);
void gc_os_take_writes(void* base, size_t size, gc_page_fn written, void* ctx);

// Return the pages of [start, start + size) to the OS / take them back zeroed. Ranges never cross a mapping.
void gc_os_decommit(void* start, size_t size);
int gc_os_commit(void* start, size_t size);

uint64_t gc_os_now_ns(void);

// The one lock of the collector: every allocation that leaves its thread's cache and every API call holds it, and
// so does a collection from start to end. Re-entrant: a root enumerator may allocate while roots are marked.
void gc_os_lock(void);
void gc_os_unlock(void);

// The collector's own bookkeeping memory (span records, maps, the mark stack): never the C runtime's heap, which
// a thread the collector suspended may hold locked. Each call holds the lock, so no stopped thread is inside.
// gc_mem_alloc and gc_mem_realloc's new bytes are zeroed.
void* gc_mem_alloc(size_t size);
void* gc_mem_realloc(void* p, size_t size);
void gc_mem_free(void* p);

// A thread the collector can stop: its handle and the highest address of its stack.
typedef struct GCOsThread
{
	void* handle;
	uint64_t id;
	const void* stackTop;
} GCOsThread;

GCOsThread gc_os_this_thread(void);
void gc_os_release_thread(GCOsThread* thread);
uint64_t gc_os_this_thread_id(void);
// The calling thread's return addresses into `frames` (room for `capacity`), from `from` out when it is among them
// (the runtime's own frames left out), else from the caller's: how many.
uint32_t gc_os_capture_stack(void** frames, uint32_t capacity, const void* from);
void gc_os_yield(void);

// Stops `thread` and waits until it has stopped (0 when it cannot be stopped); resume starts it again.
int gc_os_suspend(const GCOsThread* thread);
void gc_os_resume(const GCOsThread* thread);

// Calls `scan` for the calling thread's saved registers and for every readable range of its stack above
// the caller's frame, so every pointer the thread holds is inside one of the ranges.
typedef void (*gc_scan_range_fn)(void* ctx, const void* lo, const void* hi);
void gc_os_scan_stack(gc_scan_range_fn scan, void* ctx);
// The same for a thread gc_os_suspend stopped: its registers through `scanNow` (a copy that lives only during the
// call) and its stack from where it stopped through `scanStack` (valid until the thread resumes).
void gc_os_scan_suspended(const GCOsThread* thread, gc_scan_range_fn scanNow, gc_scan_range_fn scanStack, void* ctx);

// Memory for a collection's own work (the mark stacks), used by the collecting thread and its mark helpers while
// it runs: a heap of its own with its own lock, which no stopped thread ever holds.
void* gc_os_scratch_realloc(void* p, size_t size);
void gc_os_scratch_free(void* p);

// A thread for parallel marking (it never runs C4 code, so it is never registered or stopped) or for concurrent
// marking (it registers itself). 0 when none starts.
int gc_os_start_helper(void (*run)(void*), void* arg);
uint32_t gc_os_processor_count(void);
void gc_os_relax(void);   // a spin-wait hint
// Parks while *address holds `expected` (it may also return early); wakes every thread parked on `address`.
void gc_os_park(volatile long* address, long expected);
void gc_os_wake_all(volatile long* address);

// Calls `on_exit(data)` when the calling thread ends, unless the whole process is ending.
void gc_os_on_thread_exit(void (*on_exit)(void*), void* data);

// Prints "C4: fatal: <message>" and exits with status 1.
void gc_os_fatal(const char* message);
