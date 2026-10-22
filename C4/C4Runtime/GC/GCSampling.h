#pragma once
// Sampled allocations (c4_gc_set_alloc_sampling): an allocation is recorded with its call stack about once in every
// `every` bytes a thread allocates, at points drawn from an exponential distribution so that no allocation pattern
// lines up with them; the records wait in a ring until the profiler takes them (c4_gc_take_alloc_samples).

#include "GCRuntimeC.h"

#define GC_SAMPLE_RING 4096                    // records kept; a further one replaces the oldest
#define GC_SAMPLE_RECHECK_BYTES ((int64_t)64 << 10) // a thread looks at the setting again after this many bytes at most

// A thread's countdown to its next sample; only the thread itself touches it.
typedef struct GCSampler
{
	int64_t untilCheck;     // bytes left before the thread looks again (the allocation fast path counts it down)
	int64_t checkSpan;      // what untilCheck started from
	uint64_t untilSample;   // bytes left to the next sample, drawn under `epoch`'s setting
	uint64_t epoch;
	uint64_t random;        // xorshift64* state; 0 until first used
} GCSampler;

typedef struct GCSampling
{
	volatile uint64_t every;       // bytes between samples on average; 0: off
	volatile uint64_t epoch;       // changes with every setting, so threads draw anew
	c4_gc_alloc_sample* ring;      // GC_SAMPLE_RING records (gc_mem memory); NULL until sampling first turns on
	uint32_t first;                // the oldest record
	uint32_t count;
} GCSampling;

// Sets the average distance between samples (0 turns sampling off) and returns the previous one. Under the lock.
uint64_t gc_sampling_set(GCSampling* sampling, uint64_t every);
// Counts `bytes` against `sampler` once its fast-path countdown ran out: whether this allocation is a sample.
int gc_sampler_due(GCSampler* sampler, const GCSampling* sampling, uint64_t bytes, uint64_t seed);
// Records a sample, replacing the oldest when the ring is full. Under the lock.
void gc_sampling_push(GCSampling* sampling, const c4_gc_alloc_sample* sample);
// Moves up to `capacity` of the oldest records to `out`: how many. Under the lock.
int32_t gc_sampling_take(GCSampling* sampling, c4_gc_alloc_sample* out, int32_t capacity);
