#pragma once
// Pages for spans. Memory comes from the OS in 64 MiB arenas (or larger runs of contiguous arenas for huge
// objects); a two-level map from address to arena gives the span of any page in O(1). Free pages form runs
// that coalesce with their neighbours; runs free for a whole collection go back to the OS.

#include "GC/GCSpan.h"

#define GC_ARENA_SHIFT 26u
#define GC_ARENA_SIZE ((size_t)1 << GC_ARENA_SHIFT)
#define GC_ARENA_PAGES (1u << (GC_ARENA_SHIFT - GC_PAGE_SHIFT))
#define GC_L1_SHIFT 36u
#define GC_L1_COUNT 2048u   // user addresses are below 2^47
#define GC_L2_COUNT (1u << (GC_L1_SHIFT - GC_ARENA_SHIFT))
#define GC_FREE_BINS 128u   // bin n holds free runs of n pages; bin 0 holds every longer run

typedef struct GCArena
{
	uint8_t* base;
	GCSpan* pages[GC_ARENA_PAGES]; // a page's span; a free run lists only its first and last page
} GCArena;

typedef struct GCPageHeap
{
	GCArena** l1[GC_L1_COUNT];
	uintptr_t lo, hi;              // every arena lies in [lo, hi)
	GCSpan* bins[GC_FREE_BINS];
	GCSpan* spareSpans;            // span records ready for reuse
	size_t committedBytes;
} GCPageHeap;

// A run of `npages` pages as a span record (state and slots left to the caller); its needsZero says whether
// the pages may hold old bytes. NULL when the OS gives no more memory.
GCSpan* gc_pages_alloc(GCPageHeap* heap, size_t npages);

// The first `npages` pages of an allocated span as a span of their own; `span` keeps the rest.
GCSpan* gc_pages_split(GCPageHeap* heap, GCSpan* span, size_t npages);

// Returns a span's pages; `epoch` is the current collection count.
void gc_pages_free(GCPageHeap* heap, GCSpan* span, uint64_t epoch);

// Gives the OS back the pages of every large run that has stayed free since before `epoch`.
void gc_pages_scavenge(GCPageHeap* heap, uint64_t epoch);

static inline GCSpan* gc_pages_span_of(const GCPageHeap* heap, uintptr_t address)
{
	if (address - heap->lo >= heap->hi - heap->lo) return NULL;
	GCArena** l2 = heap->l1[address >> GC_L1_SHIFT];
	if (!l2) return NULL;
	GCArena* arena = l2[(address >> GC_ARENA_SHIFT) & (GC_L2_COUNT - 1u)];
	if (!arena) return NULL;
	return arena->pages[(address >> GC_PAGE_SHIFT) & (GC_ARENA_PAGES - 1u)];
}
