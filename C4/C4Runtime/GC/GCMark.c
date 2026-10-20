#include "GC/GCMark.h"
#include "GC/GCPlatform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// The marker that turns the bit on scans the object: alone, a plain store; beside other claims, an atomic one.
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
	if (!object.span->noscan) push(marker, base, gc_object_size(object));
}

void gc_mark_object(GCMarker* marker, GCObject object)
{
	if (marker->verifying) { verify(marker, object); return; }
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
