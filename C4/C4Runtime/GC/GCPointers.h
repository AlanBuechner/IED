#pragma once
// Pointer maps (R16). The compiler writes one per type (c4_gc_type.pointers): which words of an object may hold a managed
// pointer. Objects of such a type live in precise spans (GC_SCAN_PRECISE) that hold objects of that one type: the span
// keeps the type's map (GCSpanMap), so taking a slot writes nothing more than an unmapped allocation does, and marking
// reads only the words the map names. A thread's cache finds its span for a type by the type's record and size class
// (gc_typed_key, GCThreadCache.typed). Maps are interned by content and never freed: a span whose type's image unloads
// keeps reading its objects by it, and a marker beside the program never reads a freed map.

#include "GC/GCPtrMap.h"
#include "GCRuntimeC.h"

typedef struct GCSpanMap
{
	struct GCSpanMap* next;   // every map (GCPointerMaps.all)
	uint64_t mask;            // `masked`: the object's bits, its first word's the lowest
	uint32_t words;           // the words `bits` covers: one object's, an array's element's
	uint16_t array;           // the map repeats from `firstWords` on (`new T[n]`)
	uint16_t firstWords;      // an array's: the words before its first element
	uint32_t masked;          // one object (no array) of at most 64 words: `mask` is all of it
	uint64_t bits[];
} GCSpanMap;

typedef struct GCPointerMaps
{
	GCPtrMap byRecord;        // a type record -> its map (a)
	GCSpanMap* all;
} GCPointerMaps;

// Objects of `type` are scanned by its map; NULL, or a type without one, is scanned whole.
static inline int gc_pointers_precise(const c4_gc_type* type) { return type && type->pointers && type->words; }

// The interned map of a precise type. Under the lock.
const GCSpanMap* gc_pointers_intern(GCPointerMaps* maps, const c4_gc_type* type);

// Forgets the records in [low, high) (an image about to unload): their maps stay, for the spans that use them.
void gc_pointers_forget_in(GCPointerMaps* maps, const void* low, const void* high);

// Whether word `word` (from the slot's start) of an object read by `map` may hold a pointer.
static inline int gc_pointers_word(const GCSpanMap* map, size_t word)
{
	if (map->array)
	{
		if (word < map->firstWords) return 0;
		word = (word - map->firstWords) % map->words;
	}
	else if (word >= map->words) return 0;
	return (int)((map->bits[word >> 6] >> (word & 63u)) & 1u);
}

// The key a thread cache and the heap find a type's spans of one size class by: an object type's record (its objects
// are all of one size), an array's record and class. Records lie below 2^47.
static inline const void* gc_typed_key(const c4_gc_type* type, uint32_t spanClass)
{
	return type->array ? (const void*)((uintptr_t)type | ((uintptr_t)(spanClass + 1u) << 48)) : (const void*)type;
}

static inline uintptr_t gc_typed_key_record(const void* key) { return (uintptr_t)key & (((uintptr_t)1 << 48) - 1u); }
