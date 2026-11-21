#include "GC/GCPointers.h"

// Markers read the bits beside the writer: every access is one atomic byte.
static inline uint8_t load(const uint8_t* byte) { return __atomic_load_n(byte, __ATOMIC_RELAXED); }
static inline void store(uint8_t* byte, uint8_t value) { __atomic_store_n(byte, value, __ATOMIC_RELAXED); }

static inline uint64_t low_mask(size_t count) { return count >= 64 ? ~(uint64_t)0 : ((uint64_t)1 << count) - 1u; }

// `count` (at most 64) bits of `map` from bit `at` on, as the low bits.
static uint64_t read_map(const uint64_t* map, size_t at, size_t count)
{
	const size_t shift = at & 63u;
	uint64_t value = map[at >> 6] >> shift;
	if (shift && shift + count > 64) value |= map[(at >> 6) + 1] << (64 - shift);
	return value & low_mask(count);
}

// Bits [at, at + count) of `bits` become the low `count` (at most 64) bits of `value`.
static void store_bits(uint8_t* bits, size_t at, uint64_t value, size_t count)
{
	for (size_t done = 0; done < count;)
	{
		const size_t bit = at + done;
		const size_t part = count - done < 8u - (bit & 7u) ? count - done : 8u - (bit & 7u);
		const uint8_t mask = (uint8_t)(low_mask(part) << (bit & 7u));
		uint8_t* byte = &bits[bit >> 3];
		store(byte, (uint8_t)((load(byte) & ~mask) | (((value >> done) << (bit & 7u)) & mask)));
		done += part;
	}
}

// Bits [at, at + count) of `bits` become bits [0, count) of `map`; NULL: zero.
static void copy_bits(uint8_t* bits, size_t at, const uint64_t* map, size_t count)
{
	for (size_t done = 0; done < count; done += 64)
	{
		const size_t part = count - done < 64 ? count - done : 64;
		store_bits(bits, at + done, map ? read_map(map, done, part) : 0, part);
	}
}

void gc_pointers_prepare(GCPageHeap* pages, GCSpan* span)
{
	if (span->pointerBits) return;
	span->bitsStride = gc_pointers_stride(span->elemSize >> 3);
	span->bitsBytes = (uint32_t)(((size_t)span->nelems * span->bitsStride + 7u) & ~(size_t)7u);
	span->pointerBits = gc_pages_take_bits(pages, span->bitsBytes);
}

// Bits [at, at + words) for the elements of an array from its first one on, `type`'s map each; a part of an element at
// the end none.
static void write_elements(uint8_t* bits, size_t at, size_t words, const c4_gc_type* type)
{
	const size_t period = type->words;
	const size_t elements = words / period;
	if (period <= 64 && 64 % period == 0)
	{
		// Every 64 bits of the run read the same: the element's map repeated.
		uint64_t repeated = type->pointers[0] & low_mask(period);
		for (size_t width = period; width < 64; width *= 2) repeated |= repeated << width;
		for (size_t done = 0; done < elements * period; done += 64)
			store_bits(bits, at + done, repeated, elements * period - done < 64 ? elements * period - done : 64);
	}
	else for (size_t e = 0; e < elements; ++e) copy_bits(bits, at + e * period, type->pointers, period);
	copy_bits(bits, at + elements * period, NULL, words - elements * period);
}

void gc_pointers_write(GCSpan* span, uint32_t slot, size_t size, const c4_gc_type* type)
{
	uint8_t* bits = span->pointerBits;
	const size_t first = (size_t)slot * span->bitsStride * 8u;
	const size_t slotWords = span->elemSize >> 3;
	if (!type->array)
	{
		const size_t mapped = type->words < slotWords ? type->words : slotWords;
		copy_bits(bits, first, type->pointers, mapped);
		copy_bits(bits, first + mapped, NULL, slotWords - mapped);
		return;
	}
	const size_t objectWords = (size >> 3) < slotWords ? (size >> 3) : slotWords; // the elements end with the object
	const size_t prefix = type->firstElementWords < objectWords ? type->firstElementWords : objectWords;
	copy_bits(bits, first, NULL, prefix);
	write_elements(bits, first + prefix, objectWords - prefix, type);
	copy_bits(bits, first + objectWords, NULL, slotWords - objectWords);
}

void gc_pointers_fill_slots(GCPageHeap* pages, GCSpan* span, const c4_gc_type* type)
{
	gc_pointers_prepare(pages, span);
	for (uint32_t slot = 0; slot < span->nelems; ++slot) gc_pointers_write(span, slot, span->elemSize, type);
}
