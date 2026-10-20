#pragma once
// Pools (D-78): memory a custom allocator divides into objects of one size, tracked one by one. A pool's pages are
// spans of up to GC_SPAN_WORDS * 64 slots, so finding and marking a pool object is finding and marking any object;
// only the allocator sets their allocation bits. A collection marks the occupied objects of a keep-all pool as roots,
// asks keep callbacks about the occupied objects nothing reached, then vacates the rest and queues them for release.

#include "GCRuntimeC.h"
#include "GC/GCMark.h"

#define GC_POOL_MAX_SLOT ((size_t)1 << 20)
#define GC_POOL_CHUNK_SLOTS (GC_SPAN_WORDS * 64u)

typedef struct GCPool
{
	c4_gc_pool_desc desc;      // slot_size rounded up to 16
	uint8_t* base;
	GCSpan** chunks;           // the spans its pages are, in address order
	size_t chunkCount;
	struct GCPool* next;       // every pool (doubly linked)
	struct GCPool* prev;
} GCPool;

typedef struct GCPoolRelease
{
	GCPool* pool;
	void* object;
} GCPoolRelease;

// Objects collections vacated, waiting for their pools' release callbacks.
typedef struct GCReleaseQueue
{
	GCPoolRelease* items;
	size_t head;
	size_t count;
	size_t capacity;
} GCReleaseQueue;

// A pool of zeroed slots on the heap's pages, put on `pools`; NULL when the sizes are out of range or memory ran out.
GCPool* gc_pool_create(GCHeap* heap, GCPool** pools, const c4_gc_pool_desc* desc);
// Gives its pages back and drops its queued releases.
void gc_pool_destroy(GCHeap* heap, GCPool** pools, GCPool* pool, GCReleaseQueue* queue, uint64_t epoch);

// The slot whose start `object` is, or -1.
int64_t gc_pool_slot_of(const GCPool* pool, const void* object);
// An object occupied while a collection marks (`marked`) counts as reached, like a new allocation.
void gc_pool_set_occupied(GCPool* pool, size_t slot, int occupied, int marked);
int gc_pool_occupied(const GCPool* pool, size_t slot);

// During a collection: the occupied objects of keep-all pools, as roots.
void gc_pools_mark_kept(GCMarker* marker, const GCPool* pools);
// After the drain: each occupied object nothing reached, offered to its pool's keep callback; kept ones are marked
// and drained at once, so what they reach is reached before the next is offered.
void gc_pools_decide(GCMarker* marker, const GCPool* pools);
// The sweep: the unreached objects are vacated and queued for release, the marks cleared; the occupied bytes are
// added to `liveBytes`.
void gc_pools_sweep(const GCPool* pools, GCReleaseQueue* queue, int64_t* liveBytes);

// Takes the next queued release; 0 when there is none.
int gc_release_pop(GCReleaseQueue* queue, GCPoolRelease* release);
