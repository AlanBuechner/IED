#include "GC/GCMark.h"
#include "GC/GCPlatform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kInitialStack = 1024, kRootChunkBytes = 16384 };

typedef uintptr_t __attribute__((may_alias)) gc_word; // scanned memory has any type

// A pointer boxed in a NaN: the top 13 bits set, a tag, then its 47 bits. Words this high are nothing else: no pointer
// reaches them, and a double only as a negative NaN.
#define GC_BOXED_WORDS 0xFFF8000000000000ull
#define GC_BOXED_POINTER ((1ull << 47) - 1)

static inline uintptr_t unboxed(uintptr_t word) { return word >= GC_BOXED_WORDS ? word & GC_BOXED_POINTER : word; }

// A hash entry: its value, its key and a chain link inside the same object. A value of all ones is nil: the entry
// holds nothing, and its key, which may be long dead, stays unread.
enum { kEntryBytes = 3 * sizeof(gc_word) };
#define GC_ENTRY_NIL (~(uintptr_t)0)

static void push(GCMarker* marker, const uint8_t* start, size_t size, GCScanKind kind)
{
	if (marker->count == marker->capacity)
	{
		const size_t capacity = marker->capacity ? marker->capacity * 2 : kInitialStack;
		GCScanItem* grown = (GCScanItem*)gc_os_scratch_realloc(marker->stack, capacity * sizeof(GCScanItem));
		if (!grown) gc_os_fatal("out of memory while collecting");
		marker->stack = grown;
		marker->capacity = capacity;
	}
	marker->stack[marker->count].start = start;
	marker->stack[marker->count].size = size;
	marker->stack[marker->count].kind = kind;
	marker->count++;
}

// The marker that turns the bit on scans the object: alone, a plain store; beside other claims, an atomic one.
static GCScanKind object_kind(GCObject object) { return (GCScanKind)object.span->scanKind; }

// The object's contents go on the stack, unless it holds nothing to read; a small precise one with its span's mask.
// Inline into every marking of an object, as the whole-object push it replaces was.
static inline __attribute__((always_inline)) void push_contents(GCMarker* marker, GCObject object)
{
	const GCScanKind kind = object_kind(object);
	if (kind == GC_SCAN_NONE) return;
	const GCSpanMap* map = kind == GC_SCAN_PRECISE ? object.span->map : NULL;
	if (map && map->masked) push(marker, gc_object_base(object), (size_t)map->mask, GC_SCAN_MASKED);
	else push(marker, gc_object_base(object), gc_object_size(object), kind);
}

static int claim(GCMarker* marker, GCObject object)
{
	if (gc_object_marked(object)) return 0;
	if (marker->share || marker->atomicClaims) return gc_bit_claim(object.span->markBits, object.slot);
	gc_bit_set(object.span->markBits, object.slot);
	return 1;
}

// Verification: an object reached from the roots that the concurrent mark did not mark was lost by a missing write
// barrier, and the sweep would free it while the program still uses it.
static void verify(GCMarker* marker, GCObject object)
{
	const uint8_t* base = gc_object_base(object);
	if (!gc_object_marked(object))
	{
		char message[160];
		snprintf(message, sizeof(message), "the concurrent mark missed a reachable object at %p (%llu bytes): a store without a write barrier",
			(const void*)base, (unsigned long long)gc_object_size(object));
		gc_os_fatal(message);
	}
	if (gc_map_find(marker->verifying, base)) return;
	gc_map_upsert(marker->verifying, base);
	push_contents(marker, object);
}

void gc_mark_object(GCMarker* marker, GCObject object)
{
	if (marker->verifying) { verify(marker, object); return; }
	if (claim(marker, object)) push_contents(marker, object);
}

void gc_mark_contents(GCMarker* marker, GCObject object)
{
	push_contents(marker, object);
}

void gc_mark_word(GCMarker* marker, uintptr_t word)
{
	GCObject object = gc_heap_find(marker->heap, (const void*)word);
	if (object.span && gc_object_base(object) != marker->ignored) gc_mark_object(marker, object);
}

void gc_mark_range(GCMarker* marker, const void* lo, const void* hi)
{
	const uintptr_t heapLo = marker->heap->pages.lo;
	const uintptr_t heapExtent = marker->heap->pages.hi - heapLo;
	const uint8_t* end = (const uint8_t*)hi;
	for (const uint8_t* p = (const uint8_t*)(((uintptr_t)lo + 7) & ~(uintptr_t)7); p + sizeof(gc_word) <= end; p += sizeof(gc_word))
	{
		const uintptr_t word = unboxed(*(const gc_word*)p);
		if (word - heapLo < heapExtent) gc_mark_word(marker, word);
	}
}

static void mark_boxed(GCMarker* marker, uintptr_t boxed)
{
	const uintptr_t word = unboxed(boxed);
	if (word - marker->heap->pages.lo < marker->heap->pages.hi - marker->heap->pages.lo) gc_mark_word(marker, word);
}

static void mark_entries(GCMarker* marker, GCScanItem item)
{
	const int values = item.kind != GC_SCAN_ENTRIES_WEAK_VALUES;
	const int keys = item.kind != GC_SCAN_ENTRIES_WEAK_KEYS;
	const uint8_t* end = item.start + item.size;
	for (const uint8_t* p = item.start; p + kEntryBytes <= end; p += kEntryBytes)
	{
		const uintptr_t value = *(const gc_word*)p;
		if (value == GC_ENTRY_NIL) continue;
		if (values) mark_boxed(marker, value);
		if (keys) mark_boxed(marker, *((const gc_word*)p + 1));
	}
}

// A word that may hold a pointer, read and marked.
static inline void mark_mapped(GCMarker* marker, const gc_word* word)
{
	const uintptr_t value = unboxed(*word);
	if (value - marker->heap->pages.lo < marker->heap->pages.hi - marker->heap->pages.lo) gc_mark_word(marker, value);
}

// The words of a part of one object of a precise span that the span's map names (a span without one: every word). An
// array's map repeats to the slot's end: the words past the object are zero, and keep nothing alive.
static void mark_precise(GCMarker* marker, GCScanItem item)
{
	const GCSpan* span = gc_pages_span_of(&marker->heap->pages, (uintptr_t)item.start);
	const GCSpanMap* map = span && span->state != GC_SPAN_FREE ? span->map : NULL;
	if (!map)
	{
		gc_mark_range(marker, item.start, item.start + item.size);
		return;
	}
	const uint32_t slot = gc_span_slot(span, (size_t)(item.start - span->base));
	const size_t first = (size_t)(item.start - gc_span_slot_base(span, slot)) >> 3;
	const gc_word* words = (const gc_word*)item.start;
	const size_t count = item.size >> 3;
	if (!map->array)
	{
		for (size_t w = 0; w < count && first + w < map->words; ++w)
			if ((map->bits[(first + w) >> 6] >> ((first + w) & 63u)) & 1u) mark_mapped(marker, &words[w]);
		return;
	}
	size_t w = first < map->firstWords ? map->firstWords - first : 0;
	for (size_t e = (first + w - map->firstWords) % map->words; w < count; ++w)
	{
		if ((map->bits[e >> 6] >> (e & 63u)) & 1u) mark_mapped(marker, &words[w]);
		if (++e == map->words) e = 0;
	}
}

static void mark_masked(GCMarker* marker, GCScanItem item)
{
	const uintptr_t heapLo = marker->heap->pages.lo;
	const uintptr_t heapExtent = marker->heap->pages.hi - heapLo;
	const gc_word* words = (const gc_word*)item.start;
	for (uint64_t set = (uint64_t)item.size; set; set &= set - 1u)
	{
		const uintptr_t value = unboxed(words[__builtin_ctzll(set)]);
		if (value - heapLo < heapExtent) gc_mark_word(marker, value);
	}
}

void gc_mark_scan_item(GCMarker* marker, GCScanItem item)
{
	if (item.kind == GC_SCAN_ALL) gc_mark_range(marker, item.start, item.start + item.size);
	else if (item.kind == GC_SCAN_MASKED) mark_masked(marker, item);
	else if (item.kind == GC_SCAN_PRECISE) mark_precise(marker, item);
	else mark_entries(marker, item);
}

void gc_mark_precise_later(GCMarker* marker, const GCSpan* span, const uint8_t* base, const uint8_t* from, const uint8_t* to)
{
	const GCSpanMap* map = span->map;
	if (map && map->masked && from == base && to == base + span->elemSize) push(marker, base, (size_t)map->mask, GC_SCAN_MASKED);
	else push(marker, from, (size_t)(to - from), GC_SCAN_PRECISE);
}

void gc_mark_entries_later(GCMarker* marker, const uint8_t* base, const uint8_t* from, const uint8_t* to, GCScanKind kind)
{
	const size_t first = (size_t)(from - base) / kEntryBytes * kEntryBytes;
	const size_t last = ((size_t)(to - base) + kEntryBytes - 1) / kEntryBytes * kEntryBytes;
	push(marker, base + first, last - first, kind);
}

static const uint8_t* copied(GCMarker* marker, const uint8_t* lo, size_t size)
{
	if (marker->copyCount == marker->copyCapacity)
	{
		const size_t capacity = marker->copyCapacity ? marker->copyCapacity * 2 : 64;
		uint8_t** grown = (uint8_t**)gc_os_scratch_realloc(marker->copies, capacity * sizeof(uint8_t*));
		if (!grown) gc_os_fatal("out of memory while collecting");
		marker->copies = grown;
		marker->copyCapacity = capacity;
	}
	uint8_t* copy = (uint8_t*)gc_os_scratch_realloc(NULL, size ? size : 1);
	if (!copy) gc_os_fatal("out of memory while collecting");
	memcpy(copy, lo, size);
	marker->copies[marker->copyCount++] = copy;
	return copy;
}

void gc_marker_free_copies(GCMarker* marker)
{
	for (size_t i = 0; i < marker->copyCount; ++i) gc_os_scratch_free(marker->copies[i]);
	gc_os_scratch_free(marker->copies);
	marker->copies = NULL;
	marker->copyCount = marker->copyCapacity = 0;
}

void gc_mark_range_later(GCMarker* marker, const void* lo, const void* hi)
{
	if (marker->snapshot && hi > lo)
	{
		const uint8_t* start = (const uint8_t*)(((uintptr_t)lo + 7) & ~(uintptr_t)7);
		const size_t size = (const uint8_t*)hi > start ? (size_t)((const uint8_t*)hi - start) : 0;
		const uint8_t* copy = copied(marker, start, size);
		lo = copy;
		hi = copy + size;
	}
	const uint8_t* p = (const uint8_t*)(((uintptr_t)lo + 7) & ~(uintptr_t)7);
	const uint8_t* end = (const uint8_t*)hi;
	for (; p < end; p += kRootChunkBytes)
		push(marker, p, (size_t)(end - p) < kRootChunkBytes ? (size_t)(end - p) : kRootChunkBytes, GC_SCAN_ALL);
}

void gc_mark_root_range(GCMarker* marker, const c4_gc_root_range* range)
{
	if (!range->pointers || ((uintptr_t)range->start & 7u))
	{
		gc_mark_range_later(marker, range->start, (const uint8_t*)range->start + range->size);
		return;
	}
	const gc_word* words = (const gc_word*)range->start;
	const size_t count = (size_t)range->size >> 3;
	for (size_t w = 0; w < (count + 63u) >> 6; ++w)
	{
		uint64_t set = range->pointers[w];
		if (count - (w << 6) < 64) set &= ((uint64_t)1 << (count - (w << 6))) - 1u;
		for (; set; set &= set - 1u) mark_boxed(marker, words[(w << 6) + (size_t)__builtin_ctzll(set)]);
	}
}

void gc_mark_typed_region(GCMarker* marker, const uint8_t* start, size_t size, const GCSpanMap* map)
{
	const gc_word* words = (const gc_word*)start;
	const size_t count = size >> 3;
	if (map->masked)
	{
		for (size_t element = 0; element + map->words <= count; element += map->words)
			for (uint64_t set = map->mask; set; set &= set - 1u) mark_boxed(marker, words[element + (size_t)__builtin_ctzll(set)]);
		return;
	}
	for (size_t w = 0, e = 0; w < count; ++w)
	{
		if ((map->bits[e >> 6] >> (e & 63u)) & 1u) mark_boxed(marker, words[w]);
		if (++e == map->words) e = 0;
	}
}

void gc_mark_range_callback(void* marker, const void* lo, const void* hi)
{
	gc_mark_range((GCMarker*)marker, lo, hi);
}

void gc_mark_range_later_callback(void* marker, const void* lo, const void* hi)
{
	gc_mark_range_later((GCMarker*)marker, lo, hi);
}

void gc_mark_drain(GCMarker* marker)
{
	while (marker->count)
	{
		const GCScanItem item = marker->stack[--marker->count];
		gc_mark_scan_item(marker, item);
	}
}

void gc_marker_free(GCMarker* marker)
{
	gc_os_scratch_free(marker->stack);
	marker->stack = NULL;
	marker->count = marker->capacity = 0;
}
