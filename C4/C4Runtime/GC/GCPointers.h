#pragma once
// Pointer maps (R16). The compiler writes one per type (c4_gc_type.pointers): which words of an object may hold a managed
// pointer. Objects of such a type live in precise spans (GC_SCAN_PRECISE), which keep pointer bits for each slot, bit i
// set when the slot's word i may hold a pointer, and marking reads only those words. A slot's bits start on a byte - 1,
// 2, 4 or 8 bytes for a slot of up to 8, 16, 32 or 64 words, whole uint64s past that - so taking a slot for a small
// object writes them with one store, and marking reads them with one load.
//
// A slot's bits are written by the thread that takes the slot for an object, right after its allocation fast path (or
// under the lock): they share no byte with another slot's, so no other thread writes them meanwhile, whoever owns the
// span by then. A marker may read them at any time: one that reads a slot's earlier bits meanwhile (the thread stopped
// between the two) scans an object that is still all zero, and the stores that follow land on written pages, which a
// concurrent cycle scans again. What the bits say of a slot no object holds does not matter, so a span takes the
// bits a freed span left (GCPageHeap's spares) without clearing them.

#include "GC/GCPageHeap.h"
#include "GCRuntimeC.h"

// Objects of `type` are scanned by its map; NULL, or a type without one, is scanned whole.
static inline int gc_pointers_precise(const c4_gc_type* type) { return type && type->pointers && type->words; }

// The bytes of pointer bits a slot of `slotWords` words takes.
static inline uint32_t gc_pointers_stride(size_t slotWords)
{
	if (slotWords <= 8) return 1;
	if (slotWords <= 16) return 2;
	if (slotWords <= 32) return 4;
	return (uint32_t)((slotWords + 63u) / 64u * 8u);
}

// Bits for every slot of a precise span (small, large or a pool's). Under the lock.
void gc_pointers_prepare(GCPageHeap* pages, GCSpan* span);

// The bits of `slot` say what an object of `type` and `size` bytes taking it may hold; the rest of the slot none.
void gc_pointers_write(GCSpan* span, uint32_t slot, size_t size, const c4_gc_type* type);

// The objects whose bits one store writes: one object (not an array) of at most this many words (a slot of at most 512
// bytes, whose bits take at most 8 bytes).
#define GC_SMALL_MAPPED_WORDS 64u

static inline int gc_pointers_small(const c4_gc_type* type, size_t size)
{
	return !type->array && size <= GC_SMALL_MAPPED_WORDS * 8u;
}

// A small type's pointer bits, its first word's the lowest.
static inline uint64_t gc_pointers_low_map(const c4_gc_type* type)
{
	return type->words >= 64 ? type->pointers[0] : type->pointers[0] & (((uint64_t)1 << type->words) - 1u);
}

// The bits of a slot of a span whose bits take at most 8 bytes a slot become `mapped`: one store.
static inline void gc_pointers_store_mask(GCSpan* span, uint32_t slot, uint64_t mapped)
{
	uint8_t* at = span->pointerBits + (size_t)slot * span->bitsStride;
	switch (span->bitsStride)
	{
	case 1: __atomic_store_n(at, (uint8_t)mapped, __ATOMIC_RELAXED); break;
	case 2: __atomic_store_n((uint16_t*)at, (uint16_t)mapped, __ATOMIC_RELAXED); break;
	case 4: __atomic_store_n((uint32_t*)at, (uint32_t)mapped, __ATOMIC_RELAXED); break;
	default: __atomic_store_n((uint64_t*)at, mapped, __ATOMIC_RELAXED); break;
	}
}

// gc_pointers_write, one store for a small object.
static inline void gc_pointers_write_fast(GCSpan* span, uint32_t slot, size_t size, const c4_gc_type* type)
{
	if (gc_pointers_small(type, size) && span->bitsStride <= 8) gc_pointers_store_mask(span, slot, gc_pointers_low_map(type));
	else gc_pointers_write(span, slot, size, type);
}

// The bits of a slot of at most 64 words (a stride of at most 8 bytes), its first word's the lowest.
static inline uint64_t gc_pointers_slot_mask(const GCSpan* span, uint32_t slot)
{
	const uint8_t* at = span->pointerBits + (size_t)slot * span->bitsStride;
	switch (span->bitsStride)
	{
	case 1: return __atomic_load_n(at, __ATOMIC_RELAXED);
	case 2: return __atomic_load_n((const uint16_t*)at, __ATOMIC_RELAXED);
	case 4: return __atomic_load_n((const uint32_t*)at, __ATOMIC_RELAXED);
	default: return __atomic_load_n((const uint64_t*)at, __ATOMIC_RELAXED);
	}
}

// Every slot of a pool's span holds an object of `type`.
void gc_pointers_fill_slots(GCPageHeap* pages, GCSpan* span, const c4_gc_type* type);
