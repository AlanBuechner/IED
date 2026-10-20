#include "GC/GCParallelMark.h"
#include "GC/GCPlatform.h"
#include <string.h>

enum
{
	kBatch = 64,          // items an idle marker takes from the pool at once
	kMaxHelpers = 31
};

typedef struct HelperSlot
{
	GCMarkHelpers* helpers;
	GCMarker* marker;
} HelperSlot;

// ------------------------------------------------------------------------------------------------
// The pool

static void lock(GCMarkShare* share)
{
	while (__atomic_exchange_n(&share->lock, 1, __ATOMIC_ACQUIRE))
		while (__atomic_load_n(&share->lock, __ATOMIC_RELAXED)) gc_os_relax();
}

static void unlock(GCMarkShare* share) { __atomic_store_n(&share->lock, 0, __ATOMIC_RELEASE); }

static void reserve_pool(GCMarkShare* share, size_t needed)
{
	if (needed <= share->poolCapacity) return;
	size_t capacity = share->poolCapacity ? share->poolCapacity : kBatch;
	while (capacity < needed) capacity *= 2;
	GCScanItem* grown = (GCScanItem*)gc_os_scratch_realloc(share->pool, capacity * sizeof(GCScanItem));
	if (!grown) gc_os_fatal("out of memory while collecting");
	share->pool = grown;
	share->poolCapacity = capacity;
}

// Half of a busy marker's stack goes to the pool while a marker is idle and the pool holds less than a batch for
// each idle marker.
static void donate(GCMarker* marker, GCMarkShare* share)
{
	const long idle = __atomic_load_n(&share->idle, __ATOMIC_RELAXED);
	if (marker->count < 2 || !idle || __atomic_load_n(&share->poolCount, __ATOMIC_RELAXED) >= (size_t)idle * kBatch) return;
	lock(share);
	const size_t given = marker->count / 2;
	reserve_pool(share, share->poolCount + given);
	memcpy(share->pool + share->poolCount, marker->stack + (marker->count - given), given * sizeof(GCScanItem));
	share->poolCount += given;
	marker->count -= given;
	__atomic_store_n(&share->poolHasWork, 1, __ATOMIC_RELEASE);
	unlock(share);
}

static void take_batch(GCMarker* marker, GCMarkShare* share)
{
	const size_t taken = share->poolCount < kBatch ? share->poolCount : kBatch;
	if (marker->capacity < taken)
	{
		GCScanItem* grown = (GCScanItem*)gc_os_scratch_realloc(marker->stack, kBatch * sizeof(GCScanItem));
		if (!grown) gc_os_fatal("out of memory while collecting");
		marker->stack = grown;
		marker->capacity = kBatch;
	}
	share->poolCount -= taken;
	memcpy(marker->stack, share->pool + share->poolCount, taken * sizeof(GCScanItem));
	marker->count = taken;
	if (!share->poolCount) __atomic_store_n(&share->poolHasWork, 0, __ATOMIC_RELAXED);
}

// Work for a marker whose stack is empty; false once every marker is idle and the pool is empty.
static int refill(GCMarker* marker, GCMarkShare* share)
{
	lock(share);
	share->idle++;
	for (;;)
	{
		if (share->poolCount)
		{
			take_batch(marker, share);
			share->idle--;
			unlock(share);
			return 1;
		}
		if (share->idle == share->markers) __atomic_store_n(&share->done, 1, __ATOMIC_RELEASE);
		unlock(share);
		while (!__atomic_load_n(&share->poolHasWork, __ATOMIC_ACQUIRE) && !__atomic_load_n(&share->done, __ATOMIC_ACQUIRE)) gc_os_relax();
		if (__atomic_load_n(&share->done, __ATOMIC_ACQUIRE)) return 0;
		lock(share);
	}
}

static void drain_shared(GCMarker* marker, GCMarkShare* share)
{
	for (;;)
	{
		while (marker->count)
		{
			const GCScanItem item = marker->stack[--marker->count];
			gc_mark_range(marker, item.start, item.start + item.size);
			donate(marker, share);
		}
		if (!refill(marker, share)) return;
	}
}

// ------------------------------------------------------------------------------------------------
// Helpers

static void helper_main(void* arg)
{
	HelperSlot slot = *(HelperSlot*)arg;
	gc_os_scratch_free(arg);
	GCMarkHelpers* helpers = slot.helpers;
	long seen = 0;
	for (;;)
	{
		while (__atomic_load_n(&helpers->round, __ATOMIC_ACQUIRE) == seen) gc_os_park(&helpers->round, seen);
		seen = __atomic_load_n(&helpers->round, __ATOMIC_ACQUIRE);
		drain_shared(slot.marker, &helpers->share);
		__atomic_fetch_add(&helpers->finished, 1, __ATOMIC_RELEASE);
	}
}

uint32_t gc_mark_helpers_ready(GCMarkHelpers* helpers, GCHeap* heap, uint32_t wanted)
{
	if (wanted > kMaxHelpers) wanted = kMaxHelpers;
	if (!helpers->markers)
	{
		helpers->markers = (GCMarker*)gc_mem_alloc(kMaxHelpers * sizeof(GCMarker));
		if (!helpers->markers) return 0;
	}
	while (helpers->started < wanted)
	{
		GCMarker* marker = &helpers->markers[helpers->started];
		marker->heap = heap;
		HelperSlot* slot = (HelperSlot*)gc_os_scratch_realloc(NULL, sizeof(HelperSlot));
		if (!slot) break;
		slot->helpers = helpers;
		slot->marker = marker;
		if (!gc_os_start_helper(helper_main, slot))
		{
			gc_os_scratch_free(slot);
			break;
		}
		helpers->started++;
	}
	return helpers->started;
}

void gc_mark_drain_together(GCMarkHelpers* helpers, GCMarker* main)
{
	GCMarkShare* share = &helpers->share;
	share->poolCount = 0;
	share->poolHasWork = 0;
	share->idle = 0;
	share->done = 0;
	share->markers = (long)helpers->started + 1;
	main->share = share;
	for (uint32_t i = 0; i < helpers->started; i++) helpers->markers[i].share = share;
	__atomic_store_n(&helpers->finished, 0, __ATOMIC_RELAXED);
	__atomic_fetch_add(&helpers->round, 1, __ATOMIC_RELEASE);
	gc_os_wake_all(&helpers->round);
	drain_shared(main, share);
	while (__atomic_load_n(&helpers->finished, __ATOMIC_ACQUIRE) != (long)helpers->started) gc_os_relax();
	main->share = NULL;
	for (uint32_t i = 0; i < helpers->started; i++) helpers->markers[i].share = NULL;
}
