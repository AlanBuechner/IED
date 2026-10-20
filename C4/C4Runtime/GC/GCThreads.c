#include "GC/GCThreads.h"
#include <string.h>

// The program's thread_local roots: the compiler writes the calling thread's copies (CodeGen/GcRoots.cpp).
extern const size_t __c4_gc_thread_root_count;
extern void __c4_gc_thread_roots(c4_gc_root_range* out);

GCThread* gc_threads_add(GCThreads* threads)
{
	GCThread* thread = (GCThread*)gc_mem_alloc(sizeof(GCThread));
	thread->os = gc_os_this_thread();
	thread->localCount = __c4_gc_thread_root_count;
	if (thread->localCount)
	{
		thread->locals = (c4_gc_root_range*)gc_mem_alloc(thread->localCount * sizeof(c4_gc_root_range));
		__c4_gc_thread_roots(thread->locals);
	}
	thread->next = threads->all;
	threads->all = thread;
	threads->count++;
	return thread;
}

void gc_threads_remove(GCThreads* threads, GCThread* thread)
{
	for (GCThread** at = &threads->all; *at; at = &(*at)->next)
	{
		if (*at != thread) continue;
		*at = thread->next;
		threads->count--;
		break;
	}
	gc_os_release_thread(&thread->os);
	gc_mem_free(thread->locals);
	gc_mem_free(thread);
}

// A thread stopped inside its allocation fast path would leave its cache half changed: let it finish first.
static void stop(GCThread* thread)
{
	for (;;)
	{
		if (!gc_os_suspend(&thread->os)) gc_os_fatal("the collector could not stop a thread");
		if (!__atomic_load_n(&thread->inAllocator, __ATOMIC_ACQUIRE)) return;
		gc_os_resume(&thread->os);
		gc_os_yield();
	}
}

void gc_threads_stop(GCThreads* threads, const GCThread* self)
{
	for (GCThread* thread = threads->all; thread; thread = thread->next)
		if (thread != self) stop(thread);
}

void gc_threads_resume(GCThreads* threads, const GCThread* self)
{
	for (GCThread* thread = threads->all; thread; thread = thread->next)
		if (thread != self) gc_os_resume(&thread->os);
}

void gc_threads_mark(GCMarker* marker, const GCThreads* threads, const GCThread* self)
{
	for (const GCThread* thread = threads->all; thread; thread = thread->next)
	{
		if (thread != self) gc_os_scan_suspended(&thread->os, gc_mark_range_callback, gc_mark_range_later_callback, marker);
		for (size_t i = 0; i < thread->localCount; ++i)
			gc_mark_range_later(marker, thread->locals[i].start, (const uint8_t*)thread->locals[i].start + thread->locals[i].size);
	}
}

void gc_thread_flush_cache(GCThread* thread, GCHeap* heap, uint64_t* allocs)
{
	GCThreadCache* cache = &thread->cache;
	for (uint32_t c = 0; c < GC_SPAN_CLASSES; ++c)
	{
		if (!cache->spans[c]) continue;
		gc_heap_disown(heap, cache->spans[c]);
		cache->spans[c] = NULL;
	}
	heap->liveBytes += (int64_t)cache->allocatedBytes;
	*allocs += cache->allocs;
	cache->allocatedBytes = 0;
	cache->allocs = 0;
}

void gc_threads_flush_caches(GCThreads* threads, GCHeap* heap, uint64_t* allocs)
{
	for (GCThread* thread = threads->all; thread; thread = thread->next) gc_thread_flush_cache(thread, heap, allocs);
}

void* gc_thread_alloc_fast(GCThread* thread, const GCHeap* heap, int spanClass)
{
	// The flag goes up before the cache is read: a collection that stopped this thread with the flag down has
	// flushed the cache, so the read finds it empty and takes the slow path.
	__atomic_store_n(&thread->inAllocator, 1, __ATOMIC_SEQ_CST);
	GCSpan* span = thread->cache.spans[spanClass];
	void* p = span ? gc_heap_alloc_slot(heap, span) : NULL;
	if (p)
	{
		thread->cache.allocatedBytes += span->elemSize;
		thread->cache.allocs++;
	}
	__atomic_store_n(&thread->inAllocator, 0, __ATOMIC_RELEASE);
	return p;
}
