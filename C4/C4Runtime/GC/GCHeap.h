#pragma once
// Objects: small ones in slots of size-class spans, large ones (over GC_SMALL_MAX, or aligned beyond
// GC_SMALL_ALIGN_MAX) in a span of their own. Spans of a class that holds pointers and one that does not
// ("noscan") are kept apart, so marking can skip the noscan ones whole. A thread allocates small objects from
// spans it owns (GCThreads.h) without the collector's lock; everything else here runs under it.

#include "GC/GCPageHeap.h"
#include "GC/GCSizeClasses.h"
#include <string.h>

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
	int allocateMarked;       // set while a collection marks (also beside the program): new objects count as reached
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
static inline int gc_heap_small_class(GCAllocRequest request)
{
	const size_t align = request.align < GC_MIN_ALIGN ? GC_MIN_ALIGN : request.align;
	const size_t size = ((request.size ? request.size : 1) + align - 1) & ~(align - 1);
	if (size > GC_SMALL_MAX || align > GC_SMALL_ALIGN_MAX) return -1;
	return (int)(gc_class_of(size) * 2u + (request.noscan ? 1u : 0u));
}

// Zeroed storage for one large object; NULL when the OS gives no more memory.
void* gc_heap_alloc_large(GCHeap* heap, GCAllocRequest request);

// A span of `spanClass` with a free slot, now owned by `owner` (the calling thread's cache); NULL when the OS gives
// no more memory. gc_heap_disown gives it back.
GCSpan* gc_heap_take_span(GCHeap* heap, uint32_t spanClass, const void* owner);
void gc_heap_disown(GCHeap* heap, GCSpan* span);

// The first clear bit in [from, limit), or limit.
static inline uint32_t gc_find_clear_bit(const uint64_t* bits, uint32_t from, uint32_t limit)
{
	for (uint32_t w = from >> 6; (w << 6) < limit; ++w)
	{
		uint64_t clear = ~__atomic_load_n(&bits[w], __ATOMIC_RELAXED);
		if (w == (from >> 6)) clear &= ~(uint64_t)0 << (from & 63u);
		if (!clear) continue;
		const uint32_t i = (w << 6) + (uint32_t)__builtin_ctzll(clear);
		return i < limit ? i : limit;
	}
	return limit;
}

// A zeroed free slot of a span the calling thread owns, or NULL when it is full. No lock and no locked instruction:
// only the owner sets a span's allocation bits and raises its count. Another thread may meanwhile delete an object of
// it, clearing its bit and lowering the count atomically; should that land between the owner's read and write of the
// same word, the slot reads as taken and the count as higher until the next sweep recomputes both from the marks.
static inline void* gc_heap_alloc_slot(const GCHeap* heap, GCSpan* span)
{
	const uint32_t nelems = span->nelems;
	if (__atomic_load_n(&span->allocCount, __ATOMIC_RELAXED) >= nelems) return NULL;
	uint32_t slot = gc_find_clear_bit(span->allocBits, span->freeIndex, nelems);
	if (slot == nelems) slot = gc_find_clear_bit(span->allocBits, 0, nelems);
	if (slot == nelems) return NULL;
	uint64_t* word = &span->allocBits[slot >> 6];
	__atomic_store_n(word, __atomic_load_n(word, __ATOMIC_RELAXED) | ((uint64_t)1 << (slot & 63u)), __ATOMIC_RELEASE);
	__atomic_store_n(&span->allocCount, __atomic_load_n(&span->allocCount, __ATOMIC_RELAXED) + 1u, __ATOMIC_RELAXED);
	span->freeIndex = slot + 1;
	uint8_t* p = gc_span_slot_base(span, slot);
	if (__atomic_load_n(&span->needsZero, __ATOMIC_ACQUIRE)) memset(p, 0, span->elemSize);
	if (__atomic_load_n(&heap->allocateMarked, __ATOMIC_ACQUIRE)) gc_bit_claim(span->markBits, slot); // markers claim beside it
	return p;
}

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
