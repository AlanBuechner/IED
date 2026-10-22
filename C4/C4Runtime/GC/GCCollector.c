#include "GC/GCCollector.h"
#include "GC/GCPlatform.h"
#include <stdio.h>
#include <string.h>

void gc_state_init(GCState* gc, const c4_gc_root_range* globals)
{
	memset(gc, 0, sizeof(*gc));
	gc->globals = globals;
	gc->percent = GC_DEFAULT_PERCENT;
	gc->minTrigger = GC_MIN_TRIGGER_BYTES;
	gc->nextCollection = GC_MIN_TRIGGER_BYTES;
	gc->marker.heap = &gc->heap;
	gc->markThreads = -1;
	gc->concurrent.threshold = GC_DEFAULT_CONCURRENT_BYTES;
}

static size_t next_collection(size_t liveBytes, int32_t percent, size_t minimum)
{
	const size_t grown = liveBytes + liveBytes / 100 * (size_t)percent;
	return grown > minimum ? grown : minimum;
}

c4_gc_stats gc_stats(const GCState* gc)
{
	c4_gc_stats s;
	s.allocs = gc->allocs;
	s.collections = gc->collections;
	s.heapBytes = gc->heap.pages.committedBytes;
	s.liveBytes = gc_heap_live_bytes(&gc->heap);
	s.freeBytes = s.heapBytes > s.liveBytes ? s.heapBytes - s.liveBytes : 0;
	s.pauseNs = gc->pauseNs;
	s.lastPauseNs = gc->lastPauseNs;
	return s;
}

// ------------------------------------------------------------------------------------------------
// Collection: every other thread stays stopped from the first mark to the end of the sweep

// Helpers for this collection's drain: none for a small heap, or when the program asked for one marking thread.
uint32_t gc_mark_helpers_wanted(const GCState* gc)
{
	if (gc_heap_live_bytes(&gc->heap) < GC_PARALLEL_MARK_BYTES || gc->markThreads == 0 || gc->markThreads == 1) return 0;
	if (gc->markThreads > 1) return (uint32_t)gc->markThreads - 1u;
	const uint32_t spare = gc_os_processor_count() - 1u;
	return spare < GC_DEFAULT_MARK_HELPERS ? spare : GC_DEFAULT_MARK_HELPERS;
}

// Roots and finalizer selection mark on this thread alone (an enumerator may allocate); the bulk drains together.
static void mark(GCState* gc, const GCThread* self)
{
	gc->heap.allocateMarked = 1; // an enumerator may allocate while roots are marked
	gc_mark_roots(&gc->marker, gc, self);
	const uint32_t helpers = gc_mark_helpers_wanted(gc);
	if (helpers && gc_mark_helpers_ready(&gc->helpers, &gc->heap, helpers)) gc_mark_drain_together(&gc->helpers, &gc->marker);
	else gc_mark_drain(&gc->marker);
	gc_pools_decide(&gc->marker, gc->pools);
	gc_finalize_select(&gc->marker, &gc->finalizers, &gc->ready);
}

void gc_reclaim(GCState* gc)
{
	gc->collections++;
	gc_heap_sweep(&gc->heap, gc->collections);
	gc_pools_sweep(gc->pools, &gc->releases, &gc->heap.liveBytes);
	if (gc->census.on) gc_census_take(&gc->census, &gc->heap);
	gc->heap.allocateMarked = 0;
	gc_pages_scavenge(&gc->heap.pages, gc->collections);
	gc->nextCollection = next_collection(gc_heap_live_bytes(&gc->heap), gc->percent, gc->minTrigger);
}

void gc_stops_record(GCStops* stops, uint64_t startNs, uint64_t endNs)
{
	if (stops->count == GC_RECORDED_STOPS)
	{
		stops->stops[GC_RECORDED_STOPS - 1].endNs = endNs;
		return;
	}
	stops->stops[stops->count++] = (c4_gc_pause){ startNs, endNs };
}

void gc_collect(GCState* gc, GCThread* self)
{
	if (gc->collecting) return;
	gc->collecting = 1;
	const uint64_t start = gc_os_now_ns();
	gc_threads_stop(&gc->threads, self);
	gc_threads_flush_caches(&gc->threads, &gc->heap, &gc->allocs);
	mark(gc, self);
	gc_threads_flush_caches(&gc->threads, &gc->heap, &gc->allocs); // what an enumerator allocated meanwhile
	gc_reclaim(gc);
	gc_threads_resume(&gc->threads, self);
	const uint64_t end = gc_os_now_ns();
	gc->lastPauseNs = end - start;
	gc->lastStops.count = 0;
	gc_stops_record(&gc->lastStops, start, end);
	gc->pauseNs += gc->lastPauseNs;
	gc->collecting = 0;
	gc->collected = 1;
}

// ------------------------------------------------------------------------------------------------
// Objects

static void check_request(GCAllocRequest request)
{
	char message[128];
	if (request.size > GC_MAX_OBJECT_BYTES)
	{
		snprintf(message, sizeof(message), "allocation of %llu bytes is too large", (unsigned long long)request.size);
		gc_os_fatal(message);
	}
	if ((request.align & (request.align - 1)) != 0 || request.align > GC_PAGE_SIZE)
	{
		snprintf(message, sizeof(message), "allocation alignment %llu is not a power of two up to %llu",
			(unsigned long long)request.align, (unsigned long long)GC_PAGE_SIZE);
		gc_os_fatal(message);
	}
}

// From the thread's span of the class, refilled from the heap until one has a free slot.
static void* allocate_small(GCState* gc, GCThread* self, uint32_t spanClass)
{
	GCThreadCache* cache = &self->cache;
	for (;;)
	{
		GCSpan* span = cache->spans[spanClass];
		if (span)
		{
			void* p = gc_heap_alloc_slot(&gc->heap, span);
			if (p)
			{
				cache->allocatedBytes += span->elemSize;
				cache->allocs++;
				return p;
			}
			gc_heap_disown(&gc->heap, span);
			cache->spans[spanClass] = NULL;
		}
		span = gc_heap_take_span(&gc->heap, spanClass, cache);
		if (!span) return NULL;
		cache->spans[spanClass] = span;
	}
}

static void* allocate_once(GCState* gc, GCThread* self, GCAllocRequest request)
{
	const int spanClass = gc_heap_small_class(request);
	if (spanClass >= 0) return allocate_small(gc, self, (uint32_t)spanClass);
	void* p = gc_heap_alloc_large(&gc->heap, request);
	if (p) gc->allocs++;
	return p;
}

// An automatic collection: a concurrent cycle for a large heap when the collector's thread runs (one already running
// carries on), else a stop-the-world one.
static void start_collection(GCState* gc, GCThread* self)
{
	if (gc_concurrent_busy(gc)) return;
	GCConcurrent* c = &gc->concurrent;
	if (gc_concurrent_wanted(gc) && !c->threadRunning && gc->startCollectorThread) c->threadRunning = gc->startCollectorThread();
	if (gc_concurrent_wanted(gc) && c->threadRunning) gc_concurrent_ask(gc);
	else gc_collect(gc, self);
}

void* gc_allocate(GCState* gc, GCThread* self, GCAllocRequest request)
{
	check_request(request);
	// The heap hears of this thread's allocations now; others' come at their own refills.
	gc->heap.liveBytes += (int64_t)self->cache.allocatedBytes;
	gc->allocs += self->cache.allocs;
	self->cache.allocatedBytes = 0;
	self->cache.allocs = 0;
	if (!gc->disabled && !gc->collecting && gc_heap_live_bytes(&gc->heap) >= gc->nextCollection) start_collection(gc, self);
	void* p = allocate_once(gc, self, request);
	if (!p && gc_concurrent_busy(gc)) return NULL;
	if (!p)
	{
		gc_collect(gc, self);
		p = allocate_once(gc, self, request);
	}
	if (!p)
	{
		char message[96];
		snprintf(message, sizeof(message), "out of memory allocating %llu bytes", (unsigned long long)request.size);
		gc_os_fatal(message);
	}
	return p;
}

static gc_finalizer_fn take_finalizer(GCState* gc, const void* object)
{
	GCEntry* entry = gc_map_find(&gc->finalizers, object);
	if (!entry) return gc_finalize_unqueue(&gc->ready, object);
	gc_finalizer_fn fn = (gc_finalizer_fn)entry->a;
	gc_map_remove(&gc->finalizers, object);
	return fn;
}

void* gc_release_begin(GCState* gc, void* ptr, gc_finalizer_fn* finalizer)
{
	GCObject object = gc_heap_find(&gc->heap, ptr);
	*finalizer = NULL;
	if (!object.span || object.span->state == GC_SPAN_POOL) return NULL; // a pool's allocator ends its objects
	void* base = gc_object_base(object);
	gc_map_remove(&gc->pins, base);
	*finalizer = take_finalizer(gc, base);
	return base;
}

void gc_release_end(GCState* gc, const GCThread* self, void* base)
{
	GCObject object = gc_heap_find(&gc->heap, base); // the finalizer may have freed it itself
	if (object.span) gc_heap_release(&gc->heap, object, gc->collections, &self->cache);
}

void gc_register_finalizer(GCState* gc, void* ptr, gc_finalizer_fn fn)
{
	GCObject object = gc_heap_find(&gc->heap, ptr);
	if (!object.span || object.span->state == GC_SPAN_POOL || !fn) return;
	gc_map_upsert(&gc->finalizers, gc_object_base(object))->a = (void*)fn;
}
