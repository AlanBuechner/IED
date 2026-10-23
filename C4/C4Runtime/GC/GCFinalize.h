#pragma once
// Finalizers. An unreachable object with a finalizer is not reclaimed in the collection that finds it: it
// and everything it refers to stay alive, it moves to the ready queue, and its finalizer runs after the
// collection. An object that another unreachable finalizable object refers to waits for that one first,
// so a finalizer never sees an object whose finalizer already ran. Pointers from an object to itself do
// not keep it waiting.

#include "GC/GCMark.h"
#include <stdbool.h>
#include "GC/GCPtrMap.h"

typedef void (*gc_finalizer_fn)(void*);

typedef struct GCFinalizerQueue
{
	GCEntry* items;   // key: the object, a: its finalizer
	size_t head;
	size_t count;
	size_t capacity;
} GCFinalizerQueue;

// After the roots are marked and drained: moves every ready object from `registry` to `ready`, marked.
void gc_finalize_select(GCMarker* marker, GCPtrMap* registry, GCFinalizerQueue* ready);

// Keeps every queued object (and what it refers to) alive.
void gc_finalize_mark_queue(GCMarker* marker, const GCFinalizerQueue* ready);

// Takes the next ready object; returns 0 when the queue is empty.
int gc_finalize_pop(GCFinalizerQueue* ready, void** object, gc_finalizer_fn* fn);

// Takes `object` out of the queue (an explicit delete of an object waiting there); its finalizer, or NULL.
gc_finalizer_fn gc_finalize_unqueue(GCFinalizerQueue* ready, const void* object);

// Drops every finalizer in [low, high) from `registry` and `ready`; how many.
uint64_t gc_finalize_drop_in(GCPtrMap* registry, GCFinalizerQueue* ready, const void* low, const void* high);

// Run a finalizer or a callback C4 code registered; an exception escaping one is a fatal error (GCCallbacks.cpp).
void c4_gc_run_finalizer(gc_finalizer_fn fn, void* object);
void c4_gc_run_collection_result(void (*fn)(void*), void* userData);
void c4_gc_run_root_enumerator(void (*fn)(void*, void (*)(void*, void*), void*), void* userData, void (*visit)(void*, void*), void* visitContext);
bool c4_gc_run_pool_keep(bool (*fn)(void*, void*), void* userData, void* object);
void c4_gc_run_pool_release(void (*fn)(void*, void*), void* userData, void* object);
