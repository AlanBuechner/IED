#pragma once
// The threads that run C4 code (D-48). A collection stops every one but the collecting thread, marks from their
// registers, stacks and thread_local roots, and starts them again after the sweep. Each thread allocates small
// objects from spans its cache owns, without the lock; the collector waits out a thread inside that fast path.

#include "GC/GCPlatform.h"
#include "GC/GCHeap.h"
#include "GC/GCMark.h"
#include "GCRuntimeC.h"

typedef struct GCThreadCache
{
	GCSpan* spans[GC_SPAN_CLASSES];   // the span each size class allocates from, owned by this thread
	size_t allocatedBytes;            // since the last refill: the heap counts them then
	uint64_t allocs;
} GCThreadCache;

typedef struct GCThread
{
	GCOsThread os;
	GCThreadCache cache;
	c4_gc_root_range* locals;         // this thread's copies of the program's thread_local roots
	size_t localCount;
	volatile long inAllocator;        // inside the cache fast path
	struct GCThread* next;
} GCThread;

typedef struct GCThreads
{
	GCThread* all;
	size_t count;
} GCThreads;

// The calling thread, added once (the caller holds the lock).
GCThread* gc_threads_add(GCThreads* threads);
void gc_threads_remove(GCThreads* threads, GCThread* thread);

// Stops every thread but `self`, none of them inside its allocation fast path; resume starts them again.
void gc_threads_stop(GCThreads* threads, const GCThread* self);
void gc_threads_resume(GCThreads* threads, const GCThread* self);

// Marks from every stopped thread's registers and stack, and from every thread's thread_local roots.
void gc_threads_mark(GCMarker* marker, const GCThreads* threads, const GCThread* self);

// Hands every thread's cached spans back to the heap and adds their counts to `allocs` (before a sweep).
void gc_threads_flush_caches(GCThreads* threads, GCHeap* heap, uint64_t* allocs);
void gc_thread_flush_cache(GCThread* thread, GCHeap* heap, uint64_t* allocs);

// A small object of `spanClass` from the thread's own span, or NULL when it has none with a free slot (the slow
// path refills it under the lock). Runs without the lock, on the thread itself.
void* gc_thread_alloc_fast(GCThread* thread, const GCHeap* heap, int spanClass);
