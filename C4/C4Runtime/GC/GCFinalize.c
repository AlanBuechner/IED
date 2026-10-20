#include "GC/GCFinalize.h"
#include "GC/GCPlatform.h"
#include <stdlib.h>
#include <string.h>

static void queue_push(GCFinalizerQueue* ready, GCEntry entry)
{
	if (ready->count == ready->capacity)
	{
		const size_t capacity = ready->capacity ? ready->capacity * 2 : 64;
		GCEntry* grown = (GCEntry*)gc_mem_realloc(ready->items, capacity * sizeof(GCEntry));
		if (!grown) gc_os_fatal("out of memory for collector metadata");
		ready->items = grown;
		ready->capacity = capacity;
	}
	ready->items[ready->count++] = entry;
}

// What an unreachable finalizable object refers to stays alive; only the object's own pointers to itself
// leave it unmarked.
static void mark_referents(GCMarker* marker, GCObject object)
{
	marker->ignored = gc_object_base(object);
	gc_mark_contents(marker, object);
	gc_mark_drain(marker);
	marker->ignored = NULL;
}

void gc_finalize_select(GCMarker* marker, GCPtrMap* registry, GCFinalizerQueue* ready)
{
	for (size_t i = 0; i < registry->capacity; ++i)
	{
		if (!registry->slots[i].key) continue;
		GCObject object = gc_heap_find(marker->heap, registry->slots[i].key);
		if (object.span && !gc_object_marked(object)) mark_referents(marker, object);
	}
	const size_t firstReady = ready->count;
	for (size_t i = 0; i < registry->capacity; ++i)
	{
		if (!registry->slots[i].key) continue;
		GCObject object = gc_heap_find(marker->heap, registry->slots[i].key);
		if (!object.span || gc_object_marked(object)) continue;
		queue_push(ready, registry->slots[i]);
		gc_mark_object(marker, object);
	}
	gc_mark_drain(marker);
	for (size_t i = firstReady; i < ready->count; ++i) gc_map_remove(registry, ready->items[i].key);
}

void gc_finalize_mark_queue(GCMarker* marker, const GCFinalizerQueue* ready)
{
	for (size_t i = ready->head; i < ready->count; ++i)
	{
		GCObject object = gc_heap_find(marker->heap, ready->items[i].key);
		if (object.span) gc_mark_object(marker, object);
	}
}

int gc_finalize_pop(GCFinalizerQueue* ready, void** object, gc_finalizer_fn* fn)
{
	if (ready->head == ready->count)
	{
		ready->head = ready->count = 0;
		return 0;
	}
	const GCEntry entry = ready->items[ready->head++];
	*object = (void*)entry.key;
	*fn = (gc_finalizer_fn)entry.a;
	return 1;
}

static int in_range(const void* fn, const void* low, const void* high)
{
	return (uintptr_t)fn >= (uintptr_t)low && (uintptr_t)fn < (uintptr_t)high;
}

uint64_t gc_finalize_drop_in(GCPtrMap* registry, GCFinalizerQueue* ready, const void* low, const void* high)
{
	uint64_t dropped = 0;
	const size_t count = registry->count;
	if (count)
	{
		GCEntry* entries = gc_map_snapshot(registry); // removing while walking the slots would move entries
		for (size_t i = 0; i < count; ++i)
			if (in_range(entries[i].a, low, high)) { gc_map_remove(registry, entries[i].key); dropped++; }
		gc_mem_free(entries);
	}
	size_t kept = ready->head;
	for (size_t i = ready->head; i < ready->count; ++i)
	{
		if (in_range(ready->items[i].a, low, high)) { dropped++; continue; }
		ready->items[kept++] = ready->items[i];
	}
	ready->count = kept;
	return dropped;
}

gc_finalizer_fn gc_finalize_unqueue(GCFinalizerQueue* ready, const void* object)
{
	for (size_t i = ready->head; i < ready->count; ++i)
	{
		if (ready->items[i].key != object) continue;
		gc_finalizer_fn fn = (gc_finalizer_fn)ready->items[i].a;
		memmove(&ready->items[i], &ready->items[i + 1], (ready->count - i - 1) * sizeof(GCEntry));
		ready->count--;
		return fn;
	}
	return NULL;
}
