#include "GC/GCHeap.h"
#include <string.h>

enum { kWasteShift = 3 }; // a span leaves at most 1/8 of its bytes unused

// ------------------------------------------------------------------------------------------------
// Span lists

static void list_push(GCSpan** head, GCSpan* span)
{
	span->prev = NULL;
	span->next = *head;
	if (*head) (*head)->prev = span;
	*head = span;
}

static void list_remove(GCSpan** head, GCSpan* span)
{
	if (span->prev) span->prev->next = span->next;
	else *head = span->next;
	if (span->next) span->next->prev = span->prev;
	span->next = span->prev = NULL;
}

void gc_heap_note_free_slot(GCHeap* heap, GCSpan* span)
{
	GCClassSpans* spans = &heap->classes[span->spanClass];
	if (span->onPartial || span->owner) return;
	span->onPartial = 1;
	span->nextPartial = spans->partial;
	spans->partial = span;
}

// ------------------------------------------------------------------------------------------------
// Small objects

int gc_heap_small_class(GCAllocRequest request)
{
	const size_t align = request.align < GC_MIN_ALIGN ? GC_MIN_ALIGN : request.align;
	const size_t size = ((request.size ? request.size : 1) + align - 1) & ~(align - 1);
	if (size > GC_SMALL_MAX || align > GC_SMALL_ALIGN_MAX) return -1;
	return (int)(gc_class_of(size) * 2u + (request.noscan ? 1u : 0u));
}

static size_t pages_for_slots(uint32_t size)
{
	for (size_t n = (size + GC_PAGE_SIZE - 1) / GC_PAGE_SIZE;; ++n)
	{
		const size_t bytes = n << GC_PAGE_SHIFT;
		if (bytes % size <= bytes >> kWasteShift) return n;
	}
}

static GCSpan* new_small_span(GCHeap* heap, uint32_t spanClass)
{
	const uint32_t size = gc_class_size(spanClass >> 1);
	const size_t npages = pages_for_slots(size);
	GCSpan* span = gc_pages_alloc(&heap->pages, npages);
	if (!span) return NULL;
	span->state = GC_SPAN_SMALL;
	span->spanClass = (uint16_t)spanClass;
	span->noscan = (uint8_t)(spanClass & 1u);
	span->elemSize = size;
	span->nelems = (uint32_t)((npages << GC_PAGE_SHIFT) / size);
	span->divMagic = (uint32_t)(0xFFFFFFFFu / size) + 1u;
	list_push(&heap->classes[spanClass].all, span);
	return span;
}

GCSpan* gc_heap_take_span(GCHeap* heap, uint32_t spanClass, const void* owner)
{
	GCClassSpans* spans = &heap->classes[spanClass];
	GCSpan* span = spans->partial;
	if (span)
	{
		spans->partial = span->nextPartial;
		span->nextPartial = NULL;
		span->onPartial = 0;
	}
	else span = new_small_span(heap, spanClass);
	if (span) span->owner = owner;
	return span;
}

void gc_heap_disown(GCHeap* heap, GCSpan* span)
{
	span->owner = NULL;
	if (__atomic_load_n(&span->allocCount, __ATOMIC_RELAXED) < span->nelems) gc_heap_note_free_slot(heap, span);
}

// The first clear bit in [from, limit), or limit.
static uint32_t find_clear_bit(const uint64_t* bits, uint32_t from, uint32_t limit)
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

// Claims slot `i` unless another claim got there first.
static int claim(uint64_t* bits, uint32_t i)
{
	const uint64_t mask = (uint64_t)1 << (i & 63u);
	return (__atomic_fetch_or(&bits[i >> 6], mask, __ATOMIC_ACQ_REL) & mask) == 0;
}

static uint32_t take_slot(GCSpan* span)
{
	for (;;)
	{
		uint32_t slot = find_clear_bit(span->allocBits, span->freeIndex, span->nelems);
		if (slot == span->nelems) slot = find_clear_bit(span->allocBits, 0, span->nelems);
		if (slot == span->nelems) return slot;
		if (!claim(span->allocBits, slot)) continue;
		__atomic_fetch_add(&span->allocCount, 1u, __ATOMIC_RELAXED);
		span->freeIndex = slot + 1;
		return slot;
	}
}

void* gc_heap_alloc_slot(const GCHeap* heap, GCSpan* span)
{
	if (__atomic_load_n(&span->allocCount, __ATOMIC_RELAXED) >= span->nelems) return NULL;
	const uint32_t slot = take_slot(span);
	if (slot == span->nelems) return NULL;
	uint8_t* p = gc_span_slot_base(span, slot);
	if (__atomic_load_n(&span->needsZero, __ATOMIC_ACQUIRE)) memset(p, 0, span->elemSize);
	if (heap->allocateMarked) gc_bit_set(span->markBits, slot); // only the collecting thread runs then
	return p;
}

// ------------------------------------------------------------------------------------------------
// Large objects

void* gc_heap_alloc_large(GCHeap* heap, GCAllocRequest request)
{
	const size_t align = request.align < GC_MIN_ALIGN ? GC_MIN_ALIGN : request.align;
	const size_t size = ((request.size ? request.size : 1) + align - 1) & ~(align - 1);
	GCSpan* span = gc_pages_alloc(&heap->pages, (size + GC_PAGE_SIZE - 1) >> GC_PAGE_SHIFT);
	if (!span) return NULL;
	span->state = GC_SPAN_LARGE;
	span->noscan = (uint8_t)(request.noscan != 0);
	span->elemSize = size;
	span->nelems = 1;
	span->allocCount = 1;
	gc_bit_set(span->allocBits, 0);
	if (span->needsZero) memset(span->base, 0, size);
	if (heap->allocateMarked) gc_bit_set(span->markBits, 0);
	list_push(&heap->large, span);
	heap->liveBytes += (int64_t)size;
	return span->base;
}

void gc_heap_release(GCHeap* heap, GCObject object, uint64_t epoch, const void* releaser)
{
	GCSpan* span = object.span;
	heap->liveBytes -= (int64_t)span->elemSize;
	if (span->state == GC_SPAN_LARGE)
	{
		list_remove(&heap->large, span);
		gc_pages_free(&heap->pages, span, epoch);
		return;
	}
	// The slot reads as free only once it is marked dirty: an owner claiming it then zeroes it.
	__atomic_store_n(&span->needsZero, 1, __ATOMIC_RELEASE);
	gc_bit_clear(span->markBits, object.slot);
	__atomic_fetch_and(&span->allocBits[object.slot >> 6], ~((uint64_t)1 << (object.slot & 63u)), __ATOMIC_RELEASE);
	__atomic_fetch_sub(&span->allocCount, 1u, __ATOMIC_RELAXED);
	// Only the owner moves its own search position; another owner finds the slot when its search wraps.
	if ((!span->owner || span->owner == releaser) && object.slot < span->freeIndex) span->freeIndex = object.slot;
	gc_heap_note_free_slot(heap, span);
}

// ------------------------------------------------------------------------------------------------
// Sweep

// The marked slots become the allocated ones; returns how many there are.
static uint32_t keep_marked(GCSpan* span)
{
	uint32_t live = 0;
	uint64_t ended = 0;
	for (uint32_t w = 0; w < GC_SPAN_WORDS; ++w)
	{
		ended |= span->allocBits[w] & ~span->markBits[w];
		span->allocBits[w] = span->markBits[w];
		span->markBits[w] = 0;
		live += (uint32_t)__builtin_popcountll(span->allocBits[w]);
	}
	if (ended) span->needsZero = 1;
	span->allocCount = live;
	span->freeIndex = 0;
	span->onPartial = 0;
	span->nextPartial = NULL;
	return live;
}

static void sweep_class(GCHeap* heap, GCClassSpans* spans, uint64_t epoch)
{
	spans->partial = NULL;
	for (GCSpan* span = spans->all; span;)
	{
		GCSpan* next = span->next;
		const uint32_t live = keep_marked(span);
		if (live == 0)
		{
			list_remove(&spans->all, span);
			gc_pages_free(&heap->pages, span, epoch);
		}
		else
		{
			heap->liveBytes += (int64_t)live * (int64_t)span->elemSize;
			if (live < span->nelems) gc_heap_note_free_slot(heap, span);
		}
		span = next;
	}
}

static void sweep_large(GCHeap* heap, uint64_t epoch)
{
	for (GCSpan* span = heap->large; span;)
	{
		GCSpan* next = span->next;
		if (gc_bit(span->markBits, 0))
		{
			gc_bit_clear(span->markBits, 0);
			heap->liveBytes += (int64_t)span->elemSize;
		}
		else
		{
			list_remove(&heap->large, span);
			gc_pages_free(&heap->pages, span, epoch);
		}
		span = next;
	}
}

void gc_heap_sweep(GCHeap* heap, uint64_t epoch)
{
	heap->liveBytes = 0;
	for (uint32_t c = 0; c < GC_SPAN_CLASSES; ++c) sweep_class(heap, &heap->classes[c], epoch);
	sweep_large(heap, epoch);
}
