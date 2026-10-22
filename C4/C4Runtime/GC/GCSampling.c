#include "GC/GCSampling.h"
#include "GC/GCPlatform.h"
#include <math.h>

static uint64_t next_random(GCSampler* sampler, uint64_t seed)
{
	if (sampler->random == 0) sampler->random = seed | 1;
	uint64_t x = sampler->random;
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	sampler->random = x;
	return x * 0x2545F4914F6CDD1DULL;
}

// A distance of mean `every` from the exponential distribution, at least 1.
static uint64_t draw(GCSampler* sampler, uint64_t every, uint64_t seed)
{
	const double unit = ((double)(next_random(sampler, seed) >> 11) + 1.0) * 0x1.0p-53;   // (0, 1]
	const double distance = -log(unit) * (double)every;
	if (distance < 1.0) return 1;
	return distance >= 1.8e19 ? UINT64_MAX : (uint64_t)distance;
}

uint64_t gc_sampling_set(GCSampling* sampling, uint64_t every)
{
	if (every && !sampling->ring) sampling->ring = gc_mem_alloc(sizeof(c4_gc_alloc_sample) * GC_SAMPLE_RING);
	const uint64_t previous = sampling->every;
	__atomic_store_n(&sampling->every, every, __ATOMIC_RELAXED);
	__atomic_add_fetch(&sampling->epoch, 1, __ATOMIC_RELEASE);
	return previous;
}

int gc_sampler_due(GCSampler* sampler, const GCSampling* sampling, uint64_t bytes, uint64_t seed)
{
	const uint64_t epoch = __atomic_load_n(&sampling->epoch, __ATOMIC_ACQUIRE);
	const uint64_t every = __atomic_load_n(&sampling->every, __ATOMIC_RELAXED);
	uint64_t counted = (uint64_t)(sampler->checkSpan - sampler->untilCheck);   // since the last look, `bytes` included
	if (sampler->epoch != epoch)
	{
		// The bytes before belong to the earlier setting.
		sampler->epoch = epoch;
		sampler->untilSample = every ? draw(sampler, every, seed) : 0;
		counted = bytes;
	}
	int due = 0;
	if (every)
	{
		due = counted >= sampler->untilSample;
		sampler->untilSample = due ? draw(sampler, every, seed) : sampler->untilSample - counted;
	}
	const uint64_t span = every && sampler->untilSample < (uint64_t)GC_SAMPLE_RECHECK_BYTES ? sampler->untilSample : (uint64_t)GC_SAMPLE_RECHECK_BYTES;
	sampler->checkSpan = (int64_t)span;
	sampler->untilCheck = (int64_t)span;
	return due;
}

void gc_sampling_push(GCSampling* sampling, const c4_gc_alloc_sample* sample)
{
	if (!sampling->ring) return;
	sampling->ring[(sampling->first + sampling->count) % GC_SAMPLE_RING] = *sample;
	if (sampling->count < GC_SAMPLE_RING) sampling->count += 1;
	else sampling->first = (sampling->first + 1) % GC_SAMPLE_RING;
}

int32_t gc_sampling_take(GCSampling* sampling, c4_gc_alloc_sample* out, int32_t capacity)
{
	const uint32_t room = capacity > 0 && out ? (uint32_t)capacity : 0;
	const uint32_t taken = sampling->count < room ? sampling->count : room;
	for (uint32_t i = 0; i < taken; ++i) out[i] = sampling->ring[(sampling->first + i) % GC_SAMPLE_RING];
	sampling->first = (sampling->first + taken) % GC_SAMPLE_RING;
	sampling->count -= taken;
	return (int32_t)taken;
}
