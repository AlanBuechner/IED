#include "GC/GCPools.h"
#include "GC/GCPlatform.h"
#include <string.h>

static size_t rounded_slot(size_t size) { return ((size ? size : 1) + 15u) & ~(size_t)15u; }

static int sizes_fit(size_t slot, size_t count) { return count > 0 && slot <= GC_POOL_MAX_SLOT && count <= ((size_t)1 << 46) / slot; }

static void link_pool(GCPool** pools, GCPool* pool)
{
	pool->next = *pools;
	if (*pools) (*pools)->prev = pool;
	*pools = pool;
}

static void unlink_pool(GCPool** pools, GCPool* pool)
{
	if (pool->prev) pool->prev->next = pool->next;
	else *pools = pool->next;
	if (pool->next) pool->next->prev = pool->prev;
}

static void init_chunk(GCSpan* chunk, GCPool* pool, uint32_t slots)
{
	chunk->state = GC_SPAN_POOL;
	chunk->pool = pool;
	chunk->elemSize = pool->desc.slot_size;
	chunk->nelems = slots;
	chunk->divMagic = (uint32_t)(0xFFFFFFFFu / pool->desc.slot_size) + 1u;
	chunk->noscan = pool->desc.noscan ? 1u : 0u;
}

// The pool's run split into spans of GC_POOL_CHUNK_SLOTS slots (whole pages, as slots are multiples of 16 bytes);
// the last takes what is left.
static void carve(GCHeap* heap, GCPool* pool, GCSpan* run)
{
	const size_t chunkPages = (GC_POOL_CHUNK_SLOTS * pool->desc.slot_size) >> GC_PAGE_SHIFT;
	size_t left = pool->desc.slot_count;
	for (size_t i = 0; i < pool->chunkCount; ++i)
	{
		const int last = i + 1 == pool->chunkCount;
		GCSpan* chunk = last ? run : gc_pages_split(&heap->pages, run, chunkPages);
		const size_t slots = last ? left : GC_POOL_CHUNK_SLOTS;
		init_chunk(chunk, pool, (uint32_t)slots);
		pool->chunks[i] = chunk;
		left -= slots;
	}
}

GCPool* gc_pool_create(GCHeap* heap, GCPool** pools, const c4_gc_pool_desc* desc)
{
	const size_t slot = rounded_slot(desc->slot_size);
	if (!sizes_fit(slot, desc->slot_count)) return NULL;
	const size_t npages = (desc->slot_count * slot + GC_PAGE_SIZE - 1) >> GC_PAGE_SHIFT;
	GCSpan* run = gc_pages_alloc(&heap->pages, npages);
	if (!run) return NULL;
	if (run->needsZero) memset(run->base, 0, npages << GC_PAGE_SHIFT);
	GCPool* pool = (GCPool*)gc_mem_alloc(sizeof(GCPool));
	pool->desc = *desc;
	pool->desc.slot_size = slot;
	pool->base = run->base;
	pool->chunkCount = (desc->slot_count + GC_POOL_CHUNK_SLOTS - 1) / GC_POOL_CHUNK_SLOTS;
	pool->chunks = (GCSpan**)gc_mem_alloc(pool->chunkCount * sizeof(GCSpan*));
	carve(heap, pool, run);
	link_pool(pools, pool);
	return pool;
}

static void drop_releases(GCReleaseQueue* queue, const GCPool* pool)
{
	size_t kept = 0;
	for (size_t i = 0; i < queue->count; ++i)
	{
		const GCPoolRelease item = queue->items[(queue->head + i) % queue->capacity];
		if (item.pool != pool) queue->items[(queue->head + kept++) % queue->capacity] = item;
	}
	queue->count = kept;
}

void gc_pool_destroy(GCHeap* heap, GCPool** pools, GCPool* pool, GCReleaseQueue* queue, uint64_t epoch)
{
	drop_releases(queue, pool);
	unlink_pool(pools, pool);
	for (size_t i = 0; i < pool->chunkCount; ++i) gc_pages_free(&heap->pages, pool->chunks[i], epoch);
	gc_mem_free(pool->chunks);
	gc_mem_free(pool);
}

int64_t gc_pool_slot_of(const GCPool* pool, const void* object)
{
	const uintptr_t offset = (uintptr_t)object - (uintptr_t)pool->base;
	if ((uintptr_t)object < (uintptr_t)pool->base || offset % pool->desc.slot_size != 0) return -1;
	const size_t slot = offset / pool->desc.slot_size;
	return slot < pool->desc.slot_count ? (int64_t)slot : -1;
}

void gc_pool_set_occupied(GCPool* pool, size_t slot, int occupied, int marked)
{
	GCSpan* chunk = pool->chunks[slot / GC_POOL_CHUNK_SLOTS];
	const uint32_t bit = (uint32_t)(slot % GC_POOL_CHUNK_SLOTS);
	const uint64_t mask = (uint64_t)1 << (bit & 63u);
	if (occupied && marked) gc_bit_claim(chunk->markBits, bit);
	if (occupied) __atomic_fetch_or(&chunk->allocBits[bit >> 6], mask, __ATOMIC_RELEASE);
	else __atomic_fetch_and(&chunk->allocBits[bit >> 6], ~mask, __ATOMIC_RELEASE);
}

int gc_pool_occupied(const GCPool* pool, size_t slot)
{
	return gc_bit(pool->chunks[slot / GC_POOL_CHUNK_SLOTS]->allocBits, (uint32_t)(slot % GC_POOL_CHUNK_SLOTS));
}

// --- During a collection ----------------------------------------------------------------------------

static void mark_occupied(GCMarker* marker, GCSpan* chunk)
{
	for (uint32_t w = 0; w < GC_SPAN_WORDS; ++w)
		for (uint64_t bits = chunk->allocBits[w]; bits; bits &= bits - 1)
			gc_mark_object(marker, (GCObject){ chunk, w * 64u + (uint32_t)__builtin_ctzll(bits) });
}

void gc_pools_mark_kept(GCMarker* marker, const GCPool* pools)
{
	for (const GCPool* pool = pools; pool; pool = pool->next)
		for (size_t c = 0; pool->desc.keep_all && c < pool->chunkCount; ++c) mark_occupied(marker, pool->chunks[c]);
}

// A kept object may reach others of the chunk, so each is checked again before it is offered.
static void offer_unreached(GCMarker* marker, const GCPool* pool, GCSpan* chunk)
{
	for (uint32_t w = 0; w < GC_SPAN_WORDS; ++w)
		for (uint64_t bits = chunk->allocBits[w] & ~chunk->markBits[w]; bits; bits &= bits - 1)
		{
			const GCObject object = { chunk, w * 64u + (uint32_t)__builtin_ctzll(bits) };
			if (gc_object_marked(object) || !pool->desc.keep(pool->desc.user_data, gc_object_base(object))) continue;
			gc_mark_object(marker, object);
			gc_mark_drain(marker);
		}
}

void gc_pools_decide(GCMarker* marker, const GCPool* pools)
{
	for (const GCPool* pool = pools; pool; pool = pool->next)
		for (size_t c = 0; pool->desc.keep && !pool->desc.keep_all && c < pool->chunkCount; ++c)
			offer_unreached(marker, pool, pool->chunks[c]);
}

static void enqueue(GCReleaseQueue* queue, GCPool* pool, void* object)
{
	if (queue->count == queue->capacity)
	{
		const size_t capacity = queue->capacity ? queue->capacity * 2 : 64;
		GCPoolRelease* grown = (GCPoolRelease*)gc_mem_alloc(capacity * sizeof(GCPoolRelease));
		for (size_t i = 0; i < queue->count; ++i) grown[i] = queue->items[(queue->head + i) % queue->capacity];
		gc_mem_free(queue->items);
		queue->items = grown;
		queue->head = 0;
		queue->capacity = capacity;
	}
	queue->items[(queue->head + queue->count++) % queue->capacity] = (GCPoolRelease){ pool, object };
}

// The marked slots stay occupied; the rest are vacated and queued when the pool listens. How many stay.
static uint32_t sweep_chunk(GCSpan* chunk, GCReleaseQueue* queue)
{
	uint32_t live = 0;
	for (uint32_t w = 0; w < GC_SPAN_WORDS; ++w)
	{
		uint64_t ended = chunk->allocBits[w] & ~chunk->markBits[w];
		for (; ended && chunk->pool->desc.release; ended &= ended - 1)
			enqueue(queue, chunk->pool, gc_span_slot_base(chunk, w * 64u + (uint32_t)__builtin_ctzll(ended)));
		chunk->allocBits[w] &= chunk->markBits[w];
		chunk->markBits[w] = 0;
		live += (uint32_t)__builtin_popcountll(chunk->allocBits[w]);
	}
	return live;
}

void gc_pools_sweep(const GCPool* pools, GCReleaseQueue* queue, int64_t* liveBytes)
{
	for (const GCPool* pool = pools; pool; pool = pool->next)
		for (size_t c = 0; c < pool->chunkCount; ++c)
			*liveBytes += (int64_t)sweep_chunk(pool->chunks[c], queue) * (int64_t)pool->desc.slot_size;
}

int gc_release_pop(GCReleaseQueue* queue, GCPoolRelease* release)
{
	if (!queue->count) return 0;
	*release = queue->items[queue->head];
	queue->head = (queue->head + 1) % queue->capacity;
	queue->count--;
	return 1;
}
