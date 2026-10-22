#pragma once
// The live objects by type (c4_gc_set_census). While a census is on, every allocation notes its type's name
// (c4_alloc_typed; none from c4_alloc) in a table beside its span's slots, and the end of each collection counts the
// survivors by it. A span's table is written by the thread that owns the span (or, for a large object, by the thread
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
// Notes `type` beside the object at `p`, of a span the calling thread owns or a large span it just made.
void gc_census_note(const GCCensus* census, const GCHeap* heap, void* p, const char* type);
// Counts the heap's objects by the types noted: after a sweep, with every thread stopped and no span owned.
void gc_census_take(GCCensus* census, const GCHeap* heap);
// Copies up to `capacity` of the last census's entries to `out`: how many types it had.
int32_t gc_census_last(const GCCensus* census, c4_gc_census_entry* out, int32_t capacity);
