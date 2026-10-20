#include "GC/GCPageHeap.h"
#include "GC/GCPlatform.h"
#include <stdlib.h>
#include <string.h>

enum
{
	kSpanRecordsPerChunk = 256,
	kScavengeMinPages = 64,     // shorter runs stay committed: returning them costs more than it saves
	kCommitChunkPages = 128     // a returned run is taken back 1 MiB at a time, not one span per OS call
};

// ------------------------------------------------------------------------------------------------
// Span records

static GCSpan* new_record(GCPageHeap* heap)
{
	if (!heap->spareSpans)
	{
		GCSpan* chunk = (GCSpan*)gc_mem_alloc(kSpanRecordsPerChunk * sizeof(GCSpan));
		if (!chunk) gc_os_fatal("out of memory for collector metadata");
		for (int i = 0; i < kSpanRecordsPerChunk; ++i)
		{
			chunk[i].next = heap->spareSpans;
			heap->spareSpans = &chunk[i];
		}
	}
	GCSpan* span = heap->spareSpans;
	heap->spareSpans = span->next;
	memset(span, 0, sizeof(*span));
	return span;
}

static void retire_record(GCPageHeap* heap, GCSpan* span)
{
	span->next = heap->spareSpans;
	heap->spareSpans = span;
}

// ------------------------------------------------------------------------------------------------
// Page map

static GCArena* arena_of(const GCPageHeap* heap, uintptr_t address)
{
	GCArena** l2 = heap->l1[address >> GC_L1_SHIFT];
	return l2 ? l2[(address >> GC_ARENA_SHIFT) & (GC_L2_COUNT - 1u)] : NULL;
}

static void set_page(GCPageHeap* heap, const uint8_t* page, GCSpan* value)
{
	const uintptr_t address = (uintptr_t)page;
	arena_of(heap, address)->pages[(address >> GC_PAGE_SHIFT) & (GC_ARENA_PAGES - 1u)] = value;
}

static void set_pages(GCPageHeap* heap, const uint8_t* first, size_t npages, GCSpan* value)
{
	for (size_t i = 0; i < npages; ++i) set_page(heap, first + (i << GC_PAGE_SHIFT), value);
}

static const uint8_t* last_page(const GCSpan* run)
{
	return run->base + ((run->npages - 1) << GC_PAGE_SHIFT);
}

static void set_run_ends(GCPageHeap* heap, GCSpan* run, GCSpan* value)
{
	set_page(heap, run->base, value);
	set_page(heap, last_page(run), value);
}

static void register_arena(GCPageHeap* heap, uint8_t* base)
{
	const uintptr_t address = (uintptr_t)base;
	GCArena*** l2 = &heap->l1[address >> GC_L1_SHIFT];
	if (!*l2) *l2 = (GCArena**)gc_mem_alloc(GC_L2_COUNT * sizeof(GCArena*));
	GCArena* arena = (GCArena*)gc_mem_alloc(sizeof(GCArena));
	if (!*l2 || !arena) gc_os_fatal("out of memory for collector metadata");
	arena->base = base;
	(*l2)[(address >> GC_ARENA_SHIFT) & (GC_L2_COUNT - 1u)] = arena;
}

static void widen_range(GCPageHeap* heap, uintptr_t lo, uintptr_t hi)
{
	if (heap->hi == heap->lo) { heap->lo = lo; heap->hi = hi; return; }
	if (lo < heap->lo) heap->lo = lo;
	if (hi > heap->hi) heap->hi = hi;
}

// ------------------------------------------------------------------------------------------------
// Free runs

static GCSpan** bin_of(GCPageHeap* heap, size_t npages)
{
	return &heap->bins[npages < GC_FREE_BINS ? npages : 0];
}

static void bin_insert(GCPageHeap* heap, GCSpan* run)
{
	GCSpan** bin = bin_of(heap, run->npages);
	run->prev = NULL;
	run->next = *bin;
	if (*bin) (*bin)->prev = run;
	*bin = run;
}

static void bin_remove(GCPageHeap* heap, GCSpan* run)
{
	if (run->prev) run->prev->next = run->next;
	else *bin_of(heap, run->npages) = run->next;
	if (run->next) run->next->prev = run->prev;
	run->next = run->prev = NULL;
}

static GCSpan* free_neighbour(GCPageHeap* heap, uintptr_t address, const GCSpan* run)
{
	GCSpan* other = gc_pages_span_of(heap, address);
	return other && other->state == GC_SPAN_FREE && other->decommitted == run->decommitted ? other : NULL;
}

static void unfile(GCPageHeap* heap, GCSpan* run)
{
	bin_remove(heap, run);
	set_run_ends(heap, run, NULL);
}

// `from` lies right after `into`.
static void merge_into(GCPageHeap* heap, GCSpan* into, GCSpan* from)
{
	into->npages += from->npages;
	into->needsZero |= from->needsZero;
	if (from->freeEpoch > into->freeEpoch) into->freeEpoch = from->freeEpoch;
	retire_record(heap, from);
}

// Files a run whose pages map to nothing, merged with free neighbours in the same commit state.
static void insert_free_run(GCPageHeap* heap, GCSpan* run)
{
	GCSpan* left = free_neighbour(heap, (uintptr_t)run->base - 1, run);
	if (left)
	{
		unfile(heap, left);
		merge_into(heap, left, run);
		run = left;
	}
	GCSpan* right = free_neighbour(heap, (uintptr_t)run->base + gc_span_bytes(run), run);
	if (right)
	{
		unfile(heap, right);
		merge_into(heap, run, right);
	}
	set_run_ends(heap, run, run);
	bin_insert(heap, run);
}

static GCSpan* find_run(GCPageHeap* heap, size_t npages)
{
	for (size_t n = npages; n < GC_FREE_BINS; ++n)
		if (heap->bins[n]) return heap->bins[n];
	for (GCSpan* run = heap->bins[0]; run; run = run->next)
		if (run->npages >= npages) return run;
	return NULL;
}

// ------------------------------------------------------------------------------------------------
// Commit state (one OS call per arena: a call never crosses a mapping)

typedef int (*gc_range_op)(void* start, size_t size);

static int decommit_op(void* start, size_t size)
{
	gc_os_decommit(start, size);
	return 1;
}

static int for_each_arena_piece(uint8_t* base, size_t bytes, gc_range_op op)
{
	uint8_t* end = base + bytes;
	for (uint8_t* p = base; p < end;)
	{
		uint8_t* arenaEnd = (uint8_t*)(((uintptr_t)p & ~(uintptr_t)(GC_ARENA_SIZE - 1)) + GC_ARENA_SIZE);
		uint8_t* pieceEnd = arenaEnd < end ? arenaEnd : end;
		if (!op(p, (size_t)(pieceEnd - p))) return 0;
		p = pieceEnd;
	}
	return 1;
}

// ------------------------------------------------------------------------------------------------
// Growth and allocation

static int add_arenas(GCPageHeap* heap, size_t npages)
{
	const size_t count = (npages + GC_ARENA_PAGES - 1) / GC_ARENA_PAGES;
	const size_t bytes = count * GC_ARENA_SIZE;
	uint8_t* base = (uint8_t*)gc_os_map(bytes, GC_ARENA_SIZE);
	if (!base) return 0;
	for (size_t i = 0; i < count; ++i) register_arena(heap, base + i * GC_ARENA_SIZE);
	widen_range(heap, (uintptr_t)base, (uintptr_t)base + bytes);
	heap->committedBytes += bytes;

	GCSpan* run = new_record(heap);
	run->base = base;
	run->npages = count * GC_ARENA_PAGES;
	run->state = GC_SPAN_FREE;
	insert_free_run(heap, run);
	return 1;
}

// Leaves the first `npages` pages of an unfiled run in it and files the rest as a free run.
static void trim(GCPageHeap* heap, GCSpan* run, size_t npages)
{
	if (run->npages <= npages) return;
	GCSpan* rest = new_record(heap);
	rest->base = run->base + (npages << GC_PAGE_SHIFT);
	rest->npages = run->npages - npages;
	rest->state = GC_SPAN_FREE;
	rest->needsZero = run->needsZero;
	rest->decommitted = run->decommitted;
	rest->freeEpoch = run->freeEpoch;
	run->npages = npages;
	insert_free_run(heap, rest);
}

// Takes back the start of an unfiled returned run, at least `npages` pages; the rest stays returned.
static int recommit(GCPageHeap* heap, GCSpan* run, size_t npages)
{
	const size_t ahead = npages > kCommitChunkPages ? npages : kCommitChunkPages;
	trim(heap, run, ahead < run->npages ? ahead : run->npages);
	if (!for_each_arena_piece(run->base, gc_span_bytes(run), gc_os_commit))
	{
		insert_free_run(heap, run);
		return 0;
	}
	heap->committedBytes += gc_span_bytes(run);
	run->decommitted = 0;
	run->needsZero = 0; // recommitted pages come back zeroed
	return 1;
}

static GCSpan* fresh_span(GCPageHeap* heap, GCSpan* run)
{
	uint8_t* base = run->base;
	const size_t npages = run->npages;
	const uint8_t needsZero = run->needsZero;
	memset(run, 0, sizeof(*run));
	run->base = base;
	run->npages = npages;
	run->needsZero = needsZero;
	set_pages(heap, base, npages, run);
	return run;
}

GCSpan* gc_pages_alloc(GCPageHeap* heap, size_t npages)
{
	GCSpan* run = find_run(heap, npages);
	if (!run)
	{
		if (!add_arenas(heap, npages)) return NULL;
		run = find_run(heap, npages);
	}
	unfile(heap, run);
	if (run->decommitted && !recommit(heap, run, npages)) return NULL;
	trim(heap, run, npages);
	return fresh_span(heap, run);
}

void gc_pages_free(GCPageHeap* heap, GCSpan* span, uint64_t epoch)
{
	set_pages(heap, span->base, span->npages, NULL);
	uint8_t* base = span->base;
	const size_t npages = span->npages;
	memset(span, 0, sizeof(*span));
	span->base = base;
	span->npages = npages;
	span->state = GC_SPAN_FREE;
	span->needsZero = 1;
	span->freeEpoch = epoch;
	insert_free_run(heap, span);
}

void gc_pages_scavenge(GCPageHeap* heap, uint64_t epoch)
{
	// Gather first: returning a run re-files it (it may merge with returned neighbours).
	GCSpan* idle = NULL;
	for (uint32_t b = 0; b < GC_FREE_BINS; ++b)
		for (GCSpan* run = heap->bins[b]; run; run = run->next)
			if (!run->decommitted && run->npages >= kScavengeMinPages && run->freeEpoch + 1 < epoch)
			{
				run->nextPartial = idle;
				idle = run;
			}
	while (idle)
	{
		GCSpan* run = idle;
		idle = run->nextPartial;
		run->nextPartial = NULL;
		unfile(heap, run);
		for_each_arena_piece(run->base, gc_span_bytes(run), decommit_op);
		heap->committedBytes -= gc_span_bytes(run);
		run->decommitted = 1;
		run->needsZero = 0;
		insert_free_run(heap, run);
	}
}
