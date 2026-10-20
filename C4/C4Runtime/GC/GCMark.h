#pragma once
// Marking with an explicit stack (no recursion). Scanning is conservative: every aligned word in a scanned
// range that points into an object (anywhere inside it) marks that object. While several markers drain
// together (GCParallelMark.h) a mark bit is claimed atomically, so each object is scanned once.

#include "GC/GCHeap.h"

typedef struct GCScanItem
{
	const uint8_t* start;
	size_t size;
} GCScanItem;

struct GCMarkShare;

typedef struct GCMarker
{
	GCHeap* heap;
	GCScanItem* stack;        // objects marked but not scanned yet (the collector's scratch memory)
	size_t count;
	size_t capacity;
	const uint8_t* ignored;   // an object that its own contents do not mark (finalizer selection)
	struct GCMarkShare* share; // set while markers drain together
} GCMarker;

void gc_mark_word(GCMarker* marker, uintptr_t word);

// Scans [lo, hi) now; what it finds waits on the stack for gc_mark_drain.
void gc_mark_range(GCMarker* marker, const void* lo, const void* hi);

// Queues [lo, hi) to be scanned by the drain, in chunks the markers share; the memory must stay as it is until then.
void gc_mark_range_later(GCMarker* marker, const void* lo, const void* hi);

// gc_mark_range and gc_mark_range_later in the shape of gc_scan_range_fn (ctx is the marker).
void gc_mark_range_callback(void* marker, const void* lo, const void* hi);
void gc_mark_range_later_callback(void* marker, const void* lo, const void* hi);

void gc_mark_object(GCMarker* marker, GCObject object);

// Queues an object's contents without marking the object itself.
void gc_mark_contents(GCMarker* marker, GCObject object);

// Scans until everything marked has been scanned.
void gc_mark_drain(GCMarker* marker);

void gc_marker_free(GCMarker* marker);
