#pragma once
// A span is a run of pages: free, carved into equal slots of one size class, or holding one large object.
// Objects have no header; a span's bitmaps say which slots hold objects and which are marked.

#include <stddef.h>
#include <stdint.h>

#define GC_PAGE_SHIFT 13u
#define GC_PAGE_SIZE ((size_t)1 << GC_PAGE_SHIFT)
#define GC_SPAN_WORDS 8u   // bitmap words: a span holds at most 512 slots (one page of 16-byte slots)

typedef enum GCSpanState
{
	GC_SPAN_FREE,
	GC_SPAN_SMALL,
	GC_SPAN_LARGE
} GCSpanState;

typedef struct GCSpan
{
	uint8_t* base;
	size_t npages;
	size_t elemSize;          // bytes per slot (a large object: its size rounded to 16)
	uint32_t nelems;
	uint32_t divMagic;        // slot of an offset: (offset * divMagic) >> 32, corrected by one
	uint32_t freeIndex;       // where the search for a free slot starts
	uint32_t allocCount;
	uint16_t spanClass;       // small spans: class * 2 + noscan
	uint8_t state;
	uint8_t noscan;           // slots hold no pointers: marking does not scan them
	uint8_t needsZero;        // the memory may hold old bytes
	uint8_t onPartial;
	uint8_t decommitted;      // free runs: the pages were returned to the OS
	uint64_t freeEpoch;       // free runs: the collection that freed them
	const void* owner;        // small spans: the thread cache that allocates from it, or NULL
	struct GCSpan* next;      // the list the span is on (its class, the large list or a free bin)
	struct GCSpan* prev;
	struct GCSpan* nextPartial;
	uint64_t allocBits[GC_SPAN_WORDS];
	uint64_t markBits[GC_SPAN_WORDS];
} GCSpan;

static inline int gc_bit(const uint64_t* bits, uint32_t i) { return (int)((__atomic_load_n(&bits[i >> 6], __ATOMIC_RELAXED) >> (i & 63u)) & 1u); }
static inline void gc_bit_set(uint64_t* bits, uint32_t i) { bits[i >> 6] |= (uint64_t)1 << (i & 63u); }
static inline void gc_bit_clear(uint64_t* bits, uint32_t i) { bits[i >> 6] &= ~((uint64_t)1 << (i & 63u)); }
// Sets bit i atomically; true when this call turned it on.
static inline int gc_bit_claim(uint64_t* bits, uint32_t i)
{
	const uint64_t mask = (uint64_t)1 << (i & 63u);
	return (__atomic_fetch_or(&bits[i >> 6], mask, __ATOMIC_RELAXED) & mask) == 0;
}

static inline size_t gc_span_bytes(const GCSpan* s) { return s->npages << GC_PAGE_SHIFT; }

// The slot holding byte `offset` of the span, or nelems when the offset is past the last slot.
static inline uint32_t gc_span_slot(const GCSpan* s, size_t offset)
{
	if (s->state == GC_SPAN_LARGE) return offset < s->elemSize ? 0u : 1u;
	uint32_t slot = (uint32_t)(((uint64_t)offset * s->divMagic) >> 32);
	if ((size_t)slot * s->elemSize > offset) --slot;
	else if ((size_t)(slot + 1) * s->elemSize <= offset) ++slot;
	return slot < s->nelems ? slot : s->nelems;
}

static inline uint8_t* gc_span_slot_base(const GCSpan* s, uint32_t slot) { return s->base + (size_t)slot * s->elemSize; }
