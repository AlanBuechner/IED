#pragma once
// The threads that run C4 code (D-48). A collection stops every one but the collecting thread, marks from their
// registers, stacks and thread_local roots, and starts them again after the sweep. Each thread allocates small
// objects from spans its cache owns, without the lock; the collector waits out a thread inside that fast path.

#include "GC/GCPlatform.h"
#include "GC/GCHeap.h"
#include "GC/GCMark.h"
#include "GCRuntimeC.h"
#include "GC/GCSampling.h"

// A thread's span for one precise type and size class (gc_typed_key), in a table indexed by the key's hash.
typedef struct GCTypedSpan
{
	const void* key;
	GCSpan* span;
} GCTypedSpan;

#define GC_TYPED_SPANS 128u

// Records are 32-byte constants side by side: their addresses' bits from the fifth tell them apart; an array's class
// (the key's top bits) is folded in.
static inline uint32_t gc_typed_index(const void* key)
{
	const uintptr_t bits = (uintptr_t)key;
	return (uint32_t)((bits >> 5) ^ (bits >> 48)) & (GC_TYPED_SPANS - 1u);
}

typedef struct GCThreadCache
{
	GCSpan* spans[GC_SPAN_CLASSES];   // the span each size class allocates from, owned by this thread
	GCTypedSpan typed[GC_TYPED_SPANS]; // the spans precise objects come from, by type, owned by this thread
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
	volatile long noStop;             // inside c4_gc_nostop_enter's regions: how deep
	// The collector's own thread waiting for its next cycle: it holds nothing of the program's then, and what its frames
	// kept of the last cycle (objects it marked or swept, a finalizer's locals) must keep nothing alive (BUG-501).
	volatile long idle;
	c4_gc_alloc_counts allocated;     // since it registered, never reset; only the thread itself writes it
	GCSampler sampler;                // its countdown to the next sampled allocation
	int64_t nativeInUse;              // native bytes it took less those it gave back (others' blocks included)
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
// Gives an entry's span back to the heap.
void gc_thread_drop_typed(GCTypedSpan* entry, GCHeap* heap);
// Drops every thread's spans of the types whose records lie in [low, high) (an image about to unload; the threads are
// stopped), so a record later loaded at their address finds no span of another layout.
void gc_threads_forget_types_in(GCThreads* threads, GCHeap* heap, const void* low, const void* high);

// A small object of `spanClass` from the thread's own span, or NULL when it has none with a free slot (the slow
// path refills it under the lock). Runs without the lock, on the thread itself.
// A slot from the thread's own cache, or NULL. The flag goes up before the cache is read: a collection that stopped
// this thread with the flag down has flushed the cache, so the read finds it empty and takes the slow path. The
// collector reads the flag only once the thread has really stopped (GetThreadContext), when its stores are visible, so
// the flag needs only to stay in order with this thread's own reads and writes: compiler fences, no locked instruction.
static inline void* gc_thread_alloc_fast(GCThread* thread, const GCHeap* heap, int spanClass)
{
	__atomic_store_n(&thread->inAllocator, 1, __ATOMIC_RELAXED);
	__atomic_signal_fence(__ATOMIC_SEQ_CST);
	GCSpan* span = thread->cache.spans[spanClass];
	void* p = span ? gc_heap_alloc_slot(heap, span) : NULL;
	if (p)
	{
		thread->cache.allocatedBytes += span->elemSize;
		thread->cache.allocs++;
	}
	__atomic_signal_fence(__ATOMIC_SEQ_CST);
	__atomic_store_n(&thread->inAllocator, 0, __ATOMIC_RELAXED);
	return p;
}

// gc_thread_alloc_fast for a precise object: the slot comes from the thread's span of its type and class (`key`), so
// nothing more is written than for any object.
static inline void* gc_thread_alloc_typed(GCThread* thread, const GCHeap* heap, const void* key)
{
	__atomic_store_n(&thread->inAllocator, 1, __ATOMIC_RELAXED);
	__atomic_signal_fence(__ATOMIC_SEQ_CST);
	const GCTypedSpan* entry = &thread->cache.typed[gc_typed_index(key)];
	GCSpan* span = entry->key == key ? entry->span : NULL;
	void* p = span ? gc_heap_alloc_slot(heap, span) : NULL;
	if (p)
	{
		thread->cache.allocatedBytes += span->elemSize;
		thread->cache.allocs++;
	}
	__atomic_signal_fence(__ATOMIC_SEQ_CST);
	__atomic_store_n(&thread->inAllocator, 0, __ATOMIC_RELAXED);
	return p;
}
