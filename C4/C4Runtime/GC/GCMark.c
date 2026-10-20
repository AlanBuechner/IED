#include "GC/GCMark.h"
#include "GC/GCPlatform.h"
#include <stdlib.h>

enum { kInitialStack = 1024, kRootChunkBytes = 16384 };

typedef uintptr_t __attribute__((may_alias)) gc_word; // scanned memory has any type

static void push(GCMarker* marker, const uint8_t* start, size_t size)
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
	marker->count++;
}

// The marker that turns the bit on scans the object: alone, a plain store; together, an atomic claim.
static int claim(GCMarker* marker, GCObject object)
{
	if (gc_object_marked(object)) return 0;
	if (marker->share) return gc_bit_claim(object.span->markBits, object.slot);
	gc_bit_set(object.span->markBits, object.slot);
	return 1;
}

void gc_mark_object(GCMarker* marker, GCObject object)
{
	if (claim(marker, object) && !object.span->noscan) push(marker, gc_object_base(object), gc_object_size(object));
}

void gc_mark_contents(GCMarker* marker, GCObject object)
{
	if (!object.span->noscan) push(marker, gc_object_base(object), gc_object_size(object));
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
		const uintptr_t word = *(const gc_word*)p;
		if (word - heapLo < heapExtent) gc_mark_word(marker, word);
	}
}

void gc_mark_range_later(GCMarker* marker, const void* lo, const void* hi)
{
	const uint8_t* p = (const uint8_t*)(((uintptr_t)lo + 7) & ~(uintptr_t)7);
	const uint8_t* end = (const uint8_t*)hi;
	for (; p < end; p += kRootChunkBytes)
		push(marker, p, (size_t)(end - p) < kRootChunkBytes ? (size_t)(end - p) : kRootChunkBytes);
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
		gc_mark_range(marker, item.start, item.start + item.size);
	}
}

void gc_marker_free(GCMarker* marker)
{
	gc_os_scratch_free(marker->stack);
	marker->stack = NULL;
	marker->count = marker->capacity = 0;
}
