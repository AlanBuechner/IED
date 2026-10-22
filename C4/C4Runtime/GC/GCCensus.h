#pragma once
// The live objects by type (c4_gc_set_census). While a census is on, every allocation notes its type
// (c4_alloc_typed; none from c4_alloc) in a table beside its span's slots, and the end of each collection counts the
// survivors by it, with the native memory they hold. A span's table is written by the thread that owns the span (or, for a large object, by the thread
// that made it) and read by the collector with every thread stopped. Pool slots are not counted.

#include "GCRuntimeC.h"
#include "GC/GCHeap.h"

typedef struct GCCensus
{
	volatile int on;
	volatile uint32_t epoch;        // a span's table written under another census reads as of no type
	c4_gc_census_entry* last;       // the last census, by type name (gc_mem memory)
	int32_t count;
	int32_t capacity;
} GCCensus;

// Turns the census on or off and returns whether it was on. Under the lock.
bool gc_census_set(GCCensus* census, bool on);
// Gives `span` a table of no types for the census of `epoch` (made the first time, cleared after); false when no memory.
bool gc_census_prepare(GCSpan* span, uint32_t epoch);

// Notes `type` beside the object at `p` in `span` (one the calling thread owns, or a large span it just made). Inline:
// every allocation passes here while a census runs.
static inline void gc_census_note(const GCCensus* census, GCSpan* span, void* p, const c4_gc_type* type)
{
	if (!span || span->state == GC_SPAN_POOL) return;
	const uint32_t epoch = __atomic_load_n(&census->epoch, __ATOMIC_RELAXED);
	if ((!span->tags || span->tagsEpoch != epoch) && !gc_census_prepare(span, epoch)) return;
	span->tags[gc_span_slot(span, (size_t)((uint8_t*)p - span->base))] = type;
}
// Counts the heap's objects by the types noted, and what their types' nativeBytes say they hold: after a sweep, with
// every other thread stopped and no span owned.
void gc_census_take(GCCensus* census, const GCHeap* heap);
// Copies up to `capacity` of the last census's entries to `out`: how many types it had.
int32_t gc_census_last(const GCCensus* census, c4_gc_census_entry* out, int32_t capacity);
