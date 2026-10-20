#pragma once
// Small-object size classes: 16-byte steps up to 256, then eight steps per doubling up to 32 KiB (72
// classes, at most 1/8 of a slot wasted). Every class above 128 bytes is a multiple of 32 and every
// multiple of 64 up to 32 KiB is a class, so the class of a size rounded up to its alignment (<= 64)
// is aligned too.

#include <stddef.h>
#include <stdint.h>

#define GC_SMALL_MAX 32768u
#define GC_CLASS_COUNT 72u
#define GC_MIN_ALIGN 16u
#define GC_LINEAR_LIMIT 256u
#define GC_STEPS_PER_DOUBLING 8u

static inline uint32_t gc_floor_log2(uint64_t v)
{
	return 63u - (uint32_t)__builtin_clzll(v);
}

// `size` is 1..GC_SMALL_MAX.
static inline uint32_t gc_class_of(size_t size)
{
	if (size <= GC_LINEAR_LIMIT) return (uint32_t)((size - 1) >> 4);
	const uint32_t e = gc_floor_log2(size - 1);
	const uint32_t step = e - 3;
	return 16u + (e - 8u) * GC_STEPS_PER_DOUBLING + (uint32_t)((size - 1) >> step) - GC_STEPS_PER_DOUBLING;
}

static inline uint32_t gc_class_size(uint32_t cls)
{
	if (cls < 16u) return (cls + 1u) * 16u;
	const uint32_t group = (cls - 16u) / GC_STEPS_PER_DOUBLING;
	const uint32_t k = (cls - 16u) % GC_STEPS_PER_DOUBLING;
	return (GC_LINEAR_LIMIT << group) + (k + 1u) * ((GC_LINEAR_LIMIT / GC_STEPS_PER_DOUBLING) << group);
}
