#pragma once
// The collector's state and the operations the C API (GCApi.c) maps onto. Every operation here runs under the
// collector's lock (gc_os_lock); a collection stops every other registered thread from start to end.

#include "GCRuntimeC.h"
#include "GC/GCFinalize.h"
#include "GC/GCThreads.h"
#include "GC/GCParallelMark.h"
#include "GC/GCPools.h"
#include "GC/GCConcurrent.h"
#include "GC/GCCensus.h"

#define GC_DEFAULT_PERCENT 100
#define GC_MIN_TRIGGER_BYTES ((size_t)4 << 20)
#define GC_MAX_OBJECT_BYTES ((size_t)1 << 46)
#define GC_PARALLEL_MARK_BYTES ((size_t)8 << 20)   // a smaller heap marks faster alone than it wakes helpers
#define GC_DEFAULT_MARK_HELPERS 7u                  // at most, by default: memory bandwidth limits more
#define GC_DEFAULT_CONCURRENT_BYTES ((int64_t)16 << 20) // a smaller heap's stop-the-world pause is already short
#define GC_RECORDED_STOPS 8                         // more than a concurrent cycle makes; a further one extends the last

// The stops of one collection, oldest first.
typedef struct GCStops
{
	uint32_t count;
	c4_gc_pause stops[GC_RECORDED_STOPS];
} GCStops;

void gc_stops_record(GCStops* stops, uint64_t startNs, uint64_t endNs);

typedef struct GCState
{
	GCHeap heap;
	GCMarker marker;                    // its stack is kept between collections
	GCThreads threads;
	GCMarkHelpers helpers;
	int32_t markThreads;                // threads that mark, the collecting one included; -1: chosen by the processors
	GCPtrMap finalizers;                // object -> finalizer (a)
	GCFinalizerQueue ready;             // unreachable objects whose finalizers have not run yet
	GCPtrMap pins;                      // object -> pin count (a)
	GCPtrMap regions;                   // start -> size (a)
	GCPtrMap rootTables;                // another image's root table (key)
	GCPtrMap enumerators;               // key -> enumerator (a), user data (b)
	GCPtrMap results;                   // key -> callback (a), user data (b)
	GCConcurrent concurrent;            // concurrent marking (GCConcurrent.h)
	int (*startCollectorThread)(void);  // starts the thread that runs concurrent cycles; 0 when none starts
	GCPool* pools;                      // every pool (GCPools.h)
	GCReleaseQueue releases;            // pool objects collections vacated, for their release callbacks
	const c4_gc_root_range* globals;
	uint64_t collections;
	uint64_t allocs;
	c4_gc_alloc_counts ended;           // what threads that ended allocated
	int64_t endedNativeInUse;           // and their native bytes in use
	GCSampling sampling;                // sampled allocations (GCSampling.h)
	GCCensus census;                    // live objects by type (GCCensus.h)
	uint64_t pauseNs;
	uint64_t lastPauseNs;
	GCStops cycleStops;                 // the running collection's stops so far
	GCStops lastStops;                  // the last finished collection's
	size_t nextCollection;              // live bytes that start the next automatic collection
	size_t minTrigger;                  // at least this (C4_GC_MIN_TRIGGER lowers it for tests)
	int32_t percent;
	int disabled;
	int collecting;
	int collected;                      // a collection ended whose callbacks and finalizers have not run yet
	int runningFinalizers;              // a thread is running the ready finalizers
	int runningReleases;                // a thread is running the queued releases
	void (*statsPrinter)(const c4_gc_stats*);
} GCState;

void gc_state_init(GCState* gc, const c4_gc_root_range* globals);

// The slow path of an allocation: a large object, or a small one when `self`'s cache has no free slot. NULL when the
// heap is full while a concurrent cycle runs: finish it (gc_concurrent_finish_now, without the lock) and ask again.
void* gc_allocate(GCState* gc, GCThread* self, GCAllocRequest request);
// A stop-the-world collection; no concurrent cycle may be running.
void gc_collect(GCState* gc, GCThread* self);
// The end of every collection: the sweep, the pages given back, the next trigger.
void gc_reclaim(GCState* gc);
// How many helpers a drain of the heap now should use.
uint32_t gc_mark_helpers_wanted(const GCState* gc);
c4_gc_stats gc_stats(const GCState* gc);

// An explicit delete, in two steps so the object's finalizer runs without the lock: the object's start and the
// finalizer to run first (NULL start: `ptr` is no object), then the release.
void* gc_release_begin(GCState* gc, void* ptr, gc_finalizer_fn* finalizer);
void gc_release_end(GCState* gc, const GCThread* self, void* base);
void gc_register_finalizer(GCState* gc, void* ptr, gc_finalizer_fn fn);

// --- roots (GCRoots.c) ---
void gc_mark_roots(GCMarker* marker, const GCState* gc, const GCThread* self);
void gc_pin(GCState* gc, void* ptr);
void gc_unpin(GCState* gc, void* ptr);
