#pragma once
// Parallel marking: while every program thread is stopped, helper threads scan beside the collecting thread.
// Each marker drains its own stack; one that runs dry takes a batch from a shared pool, which a busy marker fills
// with half of its stack whenever someone is idle and the pool is empty. Marking ends when every marker is idle
// and the pool is empty. Helpers park on an address between collections and never run C4 code.

#include "GC/GCMark.h"

typedef struct GCMarkShare
{
	volatile long lock;        // a spinlock over the pool and the idle count
	GCScanItem* pool;
	size_t poolCount;
	size_t poolCapacity;
	volatile long poolHasWork; // read without the lock by idle markers
	volatile long idle;        // markers with nothing to scan
	long markers;              // how many drain together
	volatile long done;
} GCMarkShare;

typedef struct GCMarkHelpers
{
	GCMarkShare share;
	GCMarker* markers;         // one per helper
	uint32_t started;          // helpers running
	volatile long round;       // bumped to start a drain; helpers park on it
	volatile long finished;    // helpers done with this round
} GCMarkHelpers;

// The helpers a collection uses: `wanted` of them, started the first time they are asked for (fewer when the
// system starts fewer threads).
uint32_t gc_mark_helpers_ready(GCMarkHelpers* helpers, GCHeap* heap, uint32_t wanted);

// Drains `main`'s stack together with the started helpers; returns when everything reachable is marked.
void gc_mark_drain_together(GCMarkHelpers* helpers, GCMarker* main);
