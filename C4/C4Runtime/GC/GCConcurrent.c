#include "GC/GCConcurrent.h"
#include "GC/GCCollector.h"
#include "GC/GCPlatform.h"
#include <stdio.h>

enum
{
	kPrecleanPasses = 3,          // concurrent rescans of written pages before the second stop
	kFewWrittenPages = 256        // so few left to rescan that the stop may as well do it
};

int gc_concurrent_wanted(const GCState* gc)
{
	const int64_t threshold = gc->concurrent.threshold;
	return threshold >= 0 && (int64_t)gc_heap_live_bytes(&gc->heap) >= threshold;
}

int gc_concurrent_busy(const GCState* gc)
{
	return __atomic_load_n(&gc->concurrent.state, __ATOMIC_ACQUIRE) != GC_CYCLE_IDLE;
}

void gc_concurrent_ask(GCState* gc)
{
	__atomic_store_n(&gc->concurrent.state, GC_CYCLE_ASKED, __ATOMIC_RELEASE);
	gc_os_wake_all(&gc->concurrent.state);
}

void gc_concurrent_finish_now(GCState* gc)
{
	GCConcurrent* c = &gc->concurrent;
	if (__atomic_load_n(&c->state, __ATOMIC_ACQUIRE) == GC_CYCLE_IDLE) return;
	__atomic_store_n(&c->hurry, 1, __ATOMIC_RELEASE);
	for (long state = __atomic_load_n(&c->state, __ATOMIC_ACQUIRE); state != GC_CYCLE_IDLE; state = __atomic_load_n(&c->state, __ATOMIC_ACQUIRE))
		gc_os_park(&c->state, state);
}

static int hurried(const GCConcurrent* c) { return __atomic_load_n(&c->hurry, __ATOMIC_ACQUIRE) != 0; }

// --- Written pages -----------------------------------------------------------------------------------------------

typedef struct Rescan
{
	GCMarker* marker;
	const GCHeap* heap;
	size_t pages;
} Rescan;

static void rescan_part(GCMarker* marker, const uint8_t* lo, const uint8_t* hi, const uint8_t* page, const uint8_t* end)
{
	const uint8_t* from = lo > page ? lo : page;
	const uint8_t* to = hi < end ? hi : end;
	if (from < to) gc_mark_range_later(marker, from, to);
}

// The marked objects on a written page are scanned again (the part on the page): the program may have stored a pointer
// to an object nothing else marks there after the marker passed. An unmarked object is scanned whole if it is marked.
static void rescan_page(void* ctx, uint8_t* page, size_t size)
{
	Rescan* rescan = (Rescan*)ctx;
	rescan->pages++;
	const uint8_t* end = page + size;
	GCSpan* span = gc_pages_span_of(&rescan->heap->pages, (uintptr_t)page);
	if (!span || span->state == GC_SPAN_FREE || span->noscan) return;
	if (span->state == GC_SPAN_LARGE)
	{
		if (gc_bit(span->markBits, 0)) rescan_part(rescan->marker, span->base, span->base + span->elemSize, page, end);
		return;
	}
	const uint32_t first = gc_span_slot(span, (size_t)(page - span->base));
	const uint32_t last = gc_span_slot(span, (size_t)(end - 1 - span->base));
	for (uint32_t slot = first; slot <= last && slot < span->nelems; ++slot)
		if (gc_bit(span->allocBits, slot) && gc_bit(span->markBits, slot))
			rescan_part(rescan->marker, gc_span_slot_base(span, slot), gc_span_slot_base(span, slot) + span->elemSize, page, end);
}

static GCMapping* mappings(GCState* gc, size_t* count)
{
	gc_os_lock();
	GCMapping* copy = gc_pages_mappings(&gc->heap.pages, count);
	gc_os_unlock();
	return copy;
}

static void free_mappings(GCMapping* copy)
{
	gc_os_lock();
	gc_mem_free(copy);
	gc_os_unlock();
}

static void forget_writes(GCState* gc)
{
	size_t count = 0;
	GCMapping* copy = mappings(gc, &count);
	for (size_t i = 0; i < count; ++i) gc_os_forget_writes(copy[i].base, copy[i].size);
	free_mappings(copy);
}

// Queues the marked parts of every page written since the last call; how many pages that was.
static size_t rescan_written(GCState* gc)
{
	Rescan rescan = { &gc->concurrent.marker, &gc->heap, 0 };
	size_t count = 0;
	GCMapping* copy = mappings(gc, &count);
	for (size_t i = 0; i < count; ++i) gc_os_take_writes(copy[i].base, copy[i].size, rescan_page, &rescan);
	free_mappings(copy);
	return rescan.pages;
}

// --- The cycle ----------------------------------------------------------------------------------------------------

// What a cycle did, for C4_GC_TRACE.
typedef struct CycleTrace
{
	uint64_t startNs;
	uint64_t markNs;          // marking beside the program, rescans included
	uint64_t finalNs;         // the final stop
	uint32_t stops;
	size_t writtenPages;
	int hurried;
} CycleTrace;

// With the helpers that a stop-the-world drain of this heap would use (they mark beside the program too), else alone.
static void drain(GCState* gc)
{
	gc_os_lock(); // the helpers' first start takes the collector's memory
	const uint32_t wanted = gc_mark_helpers_wanted(gc);
	const uint32_t helpers = wanted ? gc_mark_helpers_ready(&gc->helpers, &gc->heap, wanted) : 0;
	gc_os_unlock();
	if (helpers) gc_mark_drain_together(&gc->helpers, &gc->concurrent.marker);
	else gc_mark_drain(&gc->concurrent.marker);
}

// A drained stack has scanned every copy of the roots: they go.
static void mark_beside_program(GCState* gc)
{
	GCConcurrent* c = &gc->concurrent;
	if (hurried(c)) return;
	drain(gc);
	if (!c->marker.count) gc_marker_free_copies(&c->marker);
}

static void stop(GCState* gc, GCThread* self)
{
	gc_os_lock();
	gc->collecting = 1;
	gc_threads_stop(&gc->threads, self);
	__atomic_store_n(&gc->heap.allocateMarked, 1, __ATOMIC_RELEASE); // a root enumerator may allocate
}

static void resume(GCState* gc, GCThread* self, uint64_t since)
{
	gc_threads_resume(&gc->threads, self);
	gc->collecting = 0;
	const uint64_t now = gc_os_now_ns();
	gc->concurrent.pauseNs += now - since;
	gc_stops_record(&gc->cycleStops, since, now);
	gc_os_unlock();
}

// The roots' ranges are copied while the program is stopped, and the copies scanned beside it: the program changes
// them, or frees a region or a thread's stack, once it runs again. Objects the program allocates meanwhile stay
// unmarked: the ones still reached at the final stop are found from the roots and the written pages, the rest go in
// this cycle.
static void snapshot_stop(GCState* gc, GCThread* self, CycleTrace* trace)
{
	const uint64_t start = gc_os_now_ns();
	stop(gc, self);
	GCMarker* marker = &gc->concurrent.marker;
	marker->heap = &gc->heap;
	marker->atomicClaims = 0;
	marker->snapshot = 1;
	gc_mark_roots(marker, gc, self);
	marker->snapshot = 0;
	marker->atomicClaims = 1; // explicit deletes clear mark bits beside the marker
	__atomic_store_n(&gc->heap.allocateMarked, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&gc->concurrent.state, GC_CYCLE_MARKING, __ATOMIC_RELEASE);
	resume(gc, self, start);
	trace->stops++;
}

// Verification (D-79): a fresh mark from the roots, over a set of its own, must reach only marked objects.
static void verify_marks(GCState* gc, GCThread* self)
{
	GCPtrMap visited = { 0 };
	GCMarker checker = { 0 };
	checker.heap = &gc->heap;
	checker.verifying = &visited;
	gc_mark_roots(&checker, gc, self);
	gc_mark_drain(&checker);
	gc_marker_free(&checker);
	gc_mem_free(visited.slots);
}

static void announce_idle(GCConcurrent* c)
{
	c->pauseNs = 0;
	__atomic_store_n(&c->hurry, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&c->state, GC_CYCLE_IDLE, __ATOMIC_RELEASE);
	gc_os_wake_all(&c->state);
}

// The program stays stopped to the end, so the roots are scanned in place by the drain.
static void final_stop(GCState* gc, GCThread* self, CycleTrace* trace)
{
	GCConcurrent* c = &gc->concurrent;
	const uint64_t start = gc_os_now_ns();
	stop(gc, self);
	__atomic_store_n(&c->state, GC_CYCLE_FINISHING, __ATOMIC_RELEASE);
	c->marker.atomicClaims = 0;
	gc_mark_roots(&c->marker, gc, self);
	trace->writtenPages += rescan_written(gc);
	drain(gc);
	gc_marker_free_copies(&c->marker);
	if (c->verify) verify_marks(gc, self);
	gc_pools_decide(&c->marker, gc->pools);
	gc_finalize_select(&c->marker, &gc->finalizers, &gc->ready);
	gc_threads_flush_caches(&gc->threads, &gc->heap, &gc->allocs);
	gc_reclaim(gc);
	const uint64_t end = gc_os_now_ns();
	trace->finalNs = end - start;
	trace->stops++;
	gc_stops_record(&gc->cycleStops, start, end);
	gc->lastStops = gc->cycleStops;
	gc->lastPauseNs = c->pauseNs + trace->finalNs;
	gc->pauseNs += gc->lastPauseNs;
	gc->collected = 1;
	announce_idle(c);
	gc_threads_resume(&gc->threads, self);
	gc->collecting = 0;
	gc_os_unlock();
}

// Each pass copies the roots again and rescans the pages written meanwhile, so the final stop finds little left.
static void mark_beside(GCState* gc, GCThread* self, CycleTrace* trace)
{
	GCConcurrent* c = &gc->concurrent;
	const uint64_t start = gc_os_now_ns();
	mark_beside_program(gc);
	for (int pass = 0; pass < kPrecleanPasses && !hurried(c); ++pass)
	{
		snapshot_stop(gc, self, trace);
		const size_t written = rescan_written(gc);
		trace->writtenPages += written;
		mark_beside_program(gc);
		if (written < kFewWrittenPages) break;
	}
	trace->hurried = hurried(c);
	trace->markNs = gc_os_now_ns() - start;
}

static void print_trace(const GCState* gc, const CycleTrace* trace)
{
	const double ms = 1e6;
	fprintf(stderr, "[gc] concurrent cycle %llu: %.2f ms, %.2f marking beside the program; %u stops, %.3f ms in all, the final %.3f; "
		"%llu written pages rescanned; %.1f MiB live%s\n", (unsigned long long)gc->collections,
		(double)(gc_os_now_ns() - trace->startNs) / ms, (double)trace->markNs / ms, trace->stops, (double)gc->lastPauseNs / ms,
		(double)trace->finalNs / ms, (unsigned long long)trace->writtenPages, (double)gc_heap_live_bytes(&gc->heap) / (1 << 20),
		trace->hurried ? "; hurried" : "");
}

void gc_concurrent_serve(GCState* gc, GCThread* self)
{
	GCConcurrent* c = &gc->concurrent;
	c->thread = self;
	while (__atomic_load_n(&c->state, __ATOMIC_ACQUIRE) != GC_CYCLE_ASKED) gc_os_park(&c->state, GC_CYCLE_IDLE);
	CycleTrace trace = { 0 };
	trace.startNs = gc_os_now_ns();
	gc->cycleStops.count = 0; // the program reads only lastStops, under the lock
	forget_writes(gc);
	snapshot_stop(gc, self, &trace);
	mark_beside(gc, self, &trace);
	final_stop(gc, self, &trace);
	if (c->trace) print_trace(gc, &trace);
}
