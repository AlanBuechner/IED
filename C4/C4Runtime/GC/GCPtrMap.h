#pragma once
// An open-addressing hash map from a non-null pointer to two pointer-sized values (linear probing with
// backward-shift deletion: no tombstones). Zero-initialized is an empty map.

#include <stddef.h>
#include <stdint.h>

typedef struct GCEntry
{
	const void* key;
	void* a;
	void* b;
} GCEntry;

typedef struct GCPtrMap
{
	GCEntry* slots;
	size_t capacity;   // zero or a power of two
	size_t count;
} GCPtrMap;

GCEntry* gc_map_find(const GCPtrMap* map, const void* key);

// The entry for `key`, added with zero values when missing.
GCEntry* gc_map_upsert(GCPtrMap* map, const void* key);

// Removes `key`; returns whether it was present.
int gc_map_remove(GCPtrMap* map, const void* key);

// A copy of every entry, for callers that run code which may edit the map. Free it with gc_mem_free.
GCEntry* gc_map_snapshot(const GCPtrMap* map);
