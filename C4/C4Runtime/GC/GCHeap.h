#pragma once
// Objects: small ones in slots of size-class spans, large ones (over GC_SMALL_MAX, or aligned beyond
// GC_SMALL_ALIGN_MAX) in a span of their own. Spans of a class that holds pointers and one that does not
// ("noscan") are kept apart, so marking can skip the noscan ones whole. A thread allocates small objects from
// spans it owns (GCThreads.h) without the collector's lock; everything else here runs under it.

#include "GC/GCPageHeap.h"
#include "GC/GCSizeClasses.h"

#define GC_SMALL_ALIGN_MAX 64u
#define GC_SPAN_CLASSES (GC_CLASS_COUNT * 2u)

typedef struct GCClassSpans
{
	GCSpan* all;      // every span of the class (doubly linked)
	GCSpan* partial;  // spans with a free slot that no thread owns
} GCClassSpans;

typedef struct GCHeap
{
	GCPageHeap pages;
	GCClassSpans classes[GC_SPAN_CLASSES];
	GCSpan* large;            // every large span (doubly linked)
	int64_t liveBytes;        // bytes of slots holding objects, as far as the heap has heard (thread caches report late)
	int allocateMarked;       // set while a collection marks: new objects count as reached
} GCHeap;

typedef struct GCObject
{
	GCSpan* span;             // NULL: no object
	uint32_t slot;
} GCObject;

typedef struct GCAllocRequest
{
	size_t size;
	size_t align;
	int noscan;
} GCAllocRequest;

// A small request's size class span index (class * 2 + noscan), or -1 for a large one.
int gc_heap_small_class(GCAllocRequest request);

// Zeroed storage for one large object; NULL when the OS gives no more memory.
void* gc_heap_alloc_large(GCHeap* heap, GCAllocRequest request);

// A span of `spanClass` with a free slot, now owned by `owner` (the calling thread's cache); NULL when the OS gives
// no more memory. gc_heap_disown gives it back.
GCSpan* gc_heap_take_span(GCHeap* heap, uint32_t spanClass, const void* owner);
void gc_heap_disown(GCHeap* heap, GCSpan* span);

// A zeroed free slot of a span the calling thread owns, or NULL when it is full. No lock: another thread may only
// free slots of it meanwhile, which the bitmap's atomic updates allow.
void* gc_heap_alloc_slot(const GCHeap* heap, GCSpan* span);

// Ends an object now (explicit `delete`) on the thread whose cache is `releaser`: its slot or span is free for
// reuse, the slot first when the span has no owner or `releaser` owns it.
void gc_heap_release(GCHeap* heap, GCObject object, uint64_t epoch, const void* releaser);

static inline GCObject gc_heap_find(const GCHeap* heap, const void* p)
{
	GCObject none = { NULL, 0 };
	GCSpan* span = gc_pages_span_of(&heap->pages, (uintptr_t)p);
	if (!span || span->state == GC_SPAN_FREE) return none;
	const uint32_t slot = gc_span_slot(span, (size_t)((const uint8_t*)p - span->base));
	if (slot >= span->nelems || !gc_bit(span->allocBits, slot)) return none;
	GCObject found = { span, slot };
	return found;
}

static inline uint8_t* gc_object_base(GCObject object) { return gc_span_slot_base(object.span, object.slot); }
static inline size_t gc_object_size(GCObject object) { return object.span->elemSize; }
static inline int gc_object_marked(GCObject object) { return gc_bit(object.span->markBits, object.slot); }

// Puts a span with a free slot on its class's partial list (a no-op if it is there or a thread owns it).
void gc_heap_note_free_slot(GCHeap* heap, GCSpan* span);

// Ends every unmarked object and clears the marks: the survivors are the allocated objects, and spans left
// empty return their pages. `epoch` is the collection count. No thread may own a span (the caches are flushed).
void gc_heap_sweep(GCHeap* heap, uint64_t epoch);

// The live bytes, never negative.
static inline size_t gc_heap_live_bytes(const GCHeap* heap) { return heap->liveBytes > 0 ? (size_t)heap->liveBytes : 0; }
