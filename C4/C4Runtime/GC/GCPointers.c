#include "GC/GCPointers.h"
#include "GC/GCPlatform.h"
#include <string.h>

static size_t map_words(const c4_gc_type* type) { return ((size_t)type->words + 63u) >> 6; }

static int same_map(const GCSpanMap* map, const c4_gc_type* type)
{
	return map->words == type->words && map->array == type->array && map->firstWords == type->firstElementWords &&
		memcmp(map->bits, type->pointers, map_words(type) * sizeof(uint64_t)) == 0;
}

static GCSpanMap* new_map(GCPointerMaps* maps, const c4_gc_type* type)
{
	const size_t count = map_words(type);
	GCSpanMap* map = (GCSpanMap*)gc_mem_alloc(sizeof(GCSpanMap) + count * sizeof(uint64_t));
	if (!map) gc_os_fatal("out of memory for the collector's pointer maps");
	memcpy(map->bits, type->pointers, count * sizeof(uint64_t));
	map->words = type->words;
	map->array = type->array;
	map->firstWords = type->firstElementWords;
	if (map->words < 64) map->bits[0] &= ((uint64_t)1 << map->words) - 1u;   // nothing past the object
	map->masked = !map->array && map->words <= 64;
	map->mask = map->masked ? map->bits[0] : 0;
	map->next = maps->all;
	maps->all = map;
	return map;
}

static const GCSpanMap* find_or_make(GCPointerMaps* maps, const c4_gc_type* type)
{
	GCEntry* known = gc_map_find(&maps->byRecord, type);
	if (known) return (const GCSpanMap*)known->a;
	GCSpanMap* map = maps->all;
	while (map && !same_map(map, type)) map = map->next;
	if (!map) map = new_map(maps, type);
	gc_map_upsert(&maps->byRecord, type)->a = map;
	return map;
}

const GCSpanMap* gc_pointers_intern(GCPointerMaps* maps, const c4_gc_type* type)
{
	if (type != maps->lastRecord)
	{
		maps->lastMap = find_or_make(maps, type);
		maps->lastRecord = type;
	}
	return maps->lastMap;
}

void gc_pointers_forget_in(GCPointerMaps* maps, const void* low, const void* high)
{
	if ((const void*)maps->lastRecord >= low && (const void*)maps->lastRecord < high) maps->lastRecord = NULL;
	for (size_t i = 0; i < maps->byRecord.capacity;)
	{
		const void* record = maps->byRecord.slots[i].key;
		// A removal shifts later entries back into this slot: look at it again.
		if (record && record >= low && record < high) gc_map_remove(&maps->byRecord, record);
		else ++i;
	}
}
