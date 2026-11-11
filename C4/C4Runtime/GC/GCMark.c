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

// The object's contents go on the stack, unless it holds nothing to read.
static void push_contents(GCMarker* marker, GCObject object)
{
	if (object_kind(object) != GC_SCAN_NONE) push(marker, gc_object_base(object), gc_object_size(object), object_kind(object));
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

void gc_mark_scan_item(GCMarker* marker, GCScanItem item)
{
	if (item.kind == GC_SCAN_ALL) gc_mark_range(marker, item.start, item.start + item.size);
	else mark_entries(marker, item);
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
