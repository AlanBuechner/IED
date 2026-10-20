#include "GC/GCPtrMap.h"
#include "GC/GCPlatform.h"
#include <stdlib.h>
#include <string.h>

enum { kInitialCapacity = 16 };

static size_t home_of(const GCPtrMap* map, const void* key)
{
	uint64_t h = (uint64_t)(uintptr_t)key;
	h ^= h >> 33;
	h *= 0xff51afd7ed558ccdull;
	h ^= h >> 33;
	return (size_t)h & (map->capacity - 1);
}

static void grow(GCPtrMap* map)
{
	const size_t capacity = map->capacity ? map->capacity * 2 : kInitialCapacity;
	GCEntry* old = map->slots;
	const size_t oldCapacity = map->capacity;
	map->slots = (GCEntry*)gc_mem_alloc(capacity * sizeof(GCEntry));
	if (!map->slots) gc_os_fatal("out of memory for collector metadata");
	map->capacity = capacity;
	map->count = 0;
	for (size_t i = 0; i < oldCapacity; ++i)
		if (old[i].key) *gc_map_upsert(map, old[i].key) = old[i];
	gc_mem_free(old);
}

GCEntry* gc_map_find(const GCPtrMap* map, const void* key)
{
	if (!map->capacity) return NULL;
	for (size_t i = home_of(map, key);; i = (i + 1) & (map->capacity - 1))
	{
		if (!map->slots[i].key) return NULL;
		if (map->slots[i].key == key) return &map->slots[i];
	}
}

GCEntry* gc_map_upsert(GCPtrMap* map, const void* key)
{
	GCEntry* found = gc_map_find(map, key);
	if (found) return found;
	if ((map->count + 1) * 4 > map->capacity * 3) grow(map);
	size_t i = home_of(map, key);
	while (map->slots[i].key) i = (i + 1) & (map->capacity - 1);
	map->slots[i].key = key;
	map->slots[i].a = NULL;
	map->slots[i].b = NULL;
	map->count++;
	return &map->slots[i];
}

// Is `home` cyclically in (hole, i]? Then the entry at i may not move into the hole.
static int stays(size_t hole, size_t home, size_t i)
{
	return hole <= i ? (hole < home && home <= i) : (hole < home || home <= i);
}

int gc_map_remove(GCPtrMap* map, const void* key)
{
	GCEntry* found = gc_map_find(map, key);
	if (!found) return 0;
	const size_t mask = map->capacity - 1;
	size_t hole = (size_t)(found - map->slots);
	for (size_t i = (hole + 1) & mask; map->slots[i].key; i = (i + 1) & mask)
	{
		if (stays(hole, home_of(map, map->slots[i].key), i)) continue;
		map->slots[hole] = map->slots[i];
		hole = i;
	}
	memset(&map->slots[hole], 0, sizeof(GCEntry));
	map->count--;
	return 1;
}

GCEntry* gc_map_snapshot(const GCPtrMap* map)
{
	GCEntry* copy = (GCEntry*)gc_mem_alloc((map->count ? map->count : 1) * sizeof(GCEntry));
	if (!copy) gc_os_fatal("out of memory for collector metadata");
	size_t n = 0;
	for (size_t i = 0; i < map->capacity; ++i)
		if (map->slots[i].key) copy[n++] = map->slots[i];
	return copy;
}
