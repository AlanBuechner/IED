#pragma once
// Marking with an explicit stack (no recursion). Every aligned word read that points into an object (anywhere inside
// it) marks that object, also a pointer boxed in a NaN (a tag over its 47 bits: a Lua value, D-85). Roots and objects
// of unknown type are read whole (conservatively); an object of a type with a pointer map only where the map says a
// pointer may be (GC_SCAN_PRECISE, GCPointers.h). While several markers drain together (GCParallelMark.h) a mark bit is claimed
// atomically, so each object is scanned once.

#include "GC/GCHeap.h"
#include "GC/GCPtrMap.h"

typedef struct GCScanItem
{
	const uint8_t* start;
	size_t size;
	GCScanKind kind;          // GC_SCAN_ALL; GC_SCAN_PRECISE (part of a precise span); GC_SCAN_MASKED; the kind of the
	                          // hash entries it covers
} GCScanItem;

// An item kind no span has: a small precise object whose pointer bits were read when it was pushed, held in `size` (bit
// i: the word at start + 8i), so its scan needs neither its size nor its span.
#define GC_SCAN_MASKED ((GCScanKind)GC_SCAN_KINDS)
#define GC_MASKED_MAX_WORDS 64u

struct GCMarkShare;

typedef struct GCMarker
{
	GCHeap* heap;
	GCScanItem* stack;        // objects marked but not scanned yet (the collector's scratch memory)
	size_t count;
	size_t capacity;
	const uint8_t* ignored;   // an object that its own contents do not mark (finalizer selection)
	struct GCMarkShare* share; // set while markers drain together
	int snapshot;             // ranges to scan later are copied now and their copies scanned later (a concurrent mark's
	                          // snapshot of the roots: the program changes or frees them once it runs again)
	uint8_t** copies;         // the copies, for gc_marker_free_copies
	size_t copyCount;
	size_t copyCapacity;
	int atomicClaims;         // others claim mark bits meanwhile (a concurrent mark: the program, its barrier)
	GCPtrMap* verifying;      // set: check that every object reached is marked, visiting each once (D-79)
} GCMarker;

void gc_mark_word(GCMarker* marker, uintptr_t word);

// Scans [lo, hi) now; what it finds waits on the stack for gc_mark_drain.
void gc_mark_range(GCMarker* marker, const void* lo, const void* hi);

// Queues [lo, hi) to be scanned by the drain, in chunks the markers share; the memory must stay as it is until then.
void gc_mark_range_later(GCMarker* marker, const void* lo, const void* hi);

// A compiler's root range (a global, a thread_local): the words its pointer map names are read now (a concurrent
// cycle's snapshot of them), a range without a map is queued whole.
void gc_mark_root_range(GCMarker* marker, const c4_gc_root_range* range);

// A scan region of elements read by `map` (the element's, back to back from `start`): the words it names are read now (a
// concurrent cycle's snapshot of them), as a mapped root's.
void gc_mark_typed_region(GCMarker* marker, const uint8_t* start, size_t size, const GCSpanMap* map);

// Queues the part [from, to) of the precise span's object at `base`: the words the span's map names; a whole small object
// with the span's mask, as marking pushes it.
void gc_mark_precise_later(GCMarker* marker, const GCSpan* span, const uint8_t* base, const uint8_t* from, const uint8_t* to);

// Queues the part [from, to) of the hash entries of an object starting at `base`, rounded out to whole entries.
void gc_mark_entries_later(GCMarker* marker, const uint8_t* base, const uint8_t* from, const uint8_t* to, GCScanKind kind);

// Scans one item of a marker's stack.
void gc_mark_scan_item(GCMarker* marker, GCScanItem item);

// gc_mark_range and gc_mark_range_later in the shape of gc_scan_range_fn (ctx is the marker).
void gc_mark_range_callback(void* marker, const void* lo, const void* hi);
void gc_mark_range_later_callback(void* marker, const void* lo, const void* hi);

void gc_mark_object(GCMarker* marker, GCObject object);

// Queues an object's contents without marking the object itself.
void gc_mark_contents(GCMarker* marker, GCObject object);

// Scans until everything marked has been scanned.
void gc_mark_drain(GCMarker* marker);

void gc_marker_free(GCMarker* marker);
// Frees the snapshot's copies (after the drain that scanned them).
void gc_marker_free_copies(GCMarker* marker);
