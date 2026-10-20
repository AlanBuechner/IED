// The exported collector API: thin entry points over the one collector of the process (the C API carries
// no context parameter, so this file owns the state). Each entry point takes the collector's lock; a small
// allocation first tries the calling thread's cache, which needs none. Finalizers and collection callbacks run
// after the lock is released, so they may block, allocate or start threads.

#include "GC/GCCollector.h"
#include "ThreadRuntimeC.h"
#include <stdio.h>

static GCState g_gc;
static int g_ready;
static _Thread_local GCThread* t_self;

// The lock is held.
static GCState* collector(void)
{
	if (!g_ready)
	{
		gc_state_init(&g_gc, __c4_gc_roots);
		g_ready = 1;
	}
	return &g_gc;
}

// The thread's C4 life ends: its thread_local objects first (they may still allocate), then its registration.
static void on_thread_exit(void* data)
{
	c4_thread_run_exits();
	GCThread* thread = (GCThread*)data;
	gc_os_lock();
	GCState* gc = collector();
	gc_thread_flush_cache(thread, &gc->heap, &gc->allocs);
	gc_threads_remove(&gc->threads, thread);
	gc_os_unlock();
	t_self = NULL;
}

// The calling thread, registered with the collector the first time it asks.
static GCThread* this_thread(void)
{
	if (t_self) return t_self;
	gc_os_lock();
	t_self = gc_threads_add(&collector()->threads);
	gc_os_unlock();
	gc_os_on_thread_exit(on_thread_exit, t_self);
	return t_self;
}

void c4_gc_register_thread(void) { this_thread(); }

// The main thread registers before the program's global initialization runs (a C initializer of the C runtime).
static int register_main_thread(void)
{
	this_thread();
	return 0;
}
__attribute__((section(".CRT$XIU"), used)) static int (*const g_registerMainThread)(void) = register_main_thread;

// ------------------------------------------------------------------------------------------------
// After a collection: the callbacks, the statistics and the ready finalizers, without the lock

static void notify_results(GCState* gc)
{
	// A callback may register or unregister callbacks: run a copy, skipping entries removed meanwhile.
	gc_os_lock();
	const size_t count = gc->results.count;
	GCEntry* callbacks = count ? gc_map_snapshot(&gc->results) : NULL;
	gc_os_unlock();
	for (size_t i = 0; i < count; ++i)
	{
		gc_os_lock();
		const GCEntry* current = gc_map_find(&gc->results, callbacks[i].key);
		const GCEntry entry = current ? *current : callbacks[i];
		gc_os_unlock();
		if (current) ((c4_gc_on_collection_fn)entry.a)(entry.b);
	}
	gc_mem_free(callbacks);
}

// One thread at a time runs the ready finalizers; a collection a finalizer starts leaves the queue to it.
static void run_finalizers(GCState* gc)
{
	gc_os_lock();
	const int busy = gc->runningFinalizers;
	gc->runningFinalizers = 1;
	gc_os_unlock();
	if (busy) return;
	for (;;)
	{
		void* object;
		gc_finalizer_fn fn;
		gc_os_lock();
		const int ready = gc_finalize_pop(&gc->ready, &object, &fn);
		if (!ready) gc->runningFinalizers = 0;
		gc_os_unlock();
		if (!ready) return;
		c4_gc_run_finalizer(fn, object);
	}
}

static void after_collection(void)
{
	GCState* gc = &g_gc;
	gc_os_lock();
	const int collected = gc->collected;
	gc->collected = 0;
	const c4_gc_stats stats = gc_stats(gc);
	void (*printer)(const c4_gc_stats*) = gc->statsPrinter;
	gc_os_unlock();
	if (!collected) return;
	notify_results(gc);
	if (printer) printer(&stats);
	run_finalizers(gc);
}

// ------------------------------------------------------------------------------------------------
// Objects

static void* allocate(GCAllocRequest request)
{
	GCThread* self = this_thread();
	const int spanClass = gc_heap_small_class(request);
	if (spanClass >= 0)
	{
		void* p = gc_thread_alloc_fast(self, &g_gc.heap, spanClass);
		if (p) return p;
	}
	gc_os_lock();
	void* p = gc_allocate(collector(), self, request);
	gc_os_unlock();
	after_collection();
	return p;
}

static GCAllocRequest request_of(size_t size, size_t align, int noscan)
{
	GCAllocRequest request = { size, align, noscan };
	return request;
}

void* c4_alloc(size_t size, size_t align) { return allocate(request_of(size, align, 0)); }
void* c4_alloc_noscan(size_t size, size_t align) { return allocate(request_of(size, align, 1)); }

void c4_free(void* ptr)
{
	if (!ptr) return;
	GCThread* self = this_thread();
	gc_finalizer_fn finalizer;
	gc_os_lock();
	void* base = gc_release_begin(collector(), ptr, &finalizer);
	gc_os_unlock();
	if (!base) return;
	if (finalizer) c4_gc_run_finalizer(finalizer, base);
	gc_os_lock();
	gc_release_end(collector(), self, base);
	gc_os_unlock();
}

void c4_gc_register_finalizer(void* obj, void (*fn)(void*))
{
	gc_os_lock();
	gc_register_finalizer(collector(), obj, fn);
	gc_os_unlock();
}

void c4_gc_collect(void)
{
	GCThread* self = this_thread();
	gc_os_lock();
	gc_collect(collector(), self);
	gc_os_unlock();
	after_collection();
}

void c4_gc_disable(void) { gc_os_lock(); collector()->disabled = 1; gc_os_unlock(); }
void c4_gc_enable(void) { gc_os_lock(); collector()->disabled = 0; gc_os_unlock(); }

int32_t c4_gc_set_mark_threads(int32_t threads)
{
	gc_os_lock();
	GCState* gc = collector();
	const int32_t previous = gc->markThreads;
	gc->markThreads = threads < -1 ? -1 : threads;
	gc_os_unlock();
	return previous;
}

int32_t c4_gc_set_percent(int32_t percent)
{
	gc_os_lock();
	GCState* gc = collector();
	const int32_t previous = gc->percent;
	gc->percent = percent < 1 ? 1 : percent;
	gc_os_unlock();
	return previous;
}

// ------------------------------------------------------------------------------------------------
// Roots, pins and callbacks

void c4_gc_register_region(const void* start, size_t size)
{
	if (!start || !size) return;
	gc_os_lock();
	gc_map_upsert(&collector()->regions, start)->a = (void*)size;
	gc_os_unlock();
}

void c4_gc_unregister_region(const void* start)
{
	gc_os_lock();
	gc_map_remove(&collector()->regions, start);
	gc_os_unlock();
}

void c4_gc_add_root_table(const c4_gc_root_range* table)
{
	if (!table) return;
	gc_os_lock();
	gc_map_upsert(&collector()->rootTables, table);
	gc_os_unlock();
}

void c4_gc_remove_root_table(const c4_gc_root_range* table)
{
	gc_os_lock();
	gc_map_remove(&collector()->rootTables, table);
	gc_os_unlock();
}

uint64_t c4_gc_drop_finalizers_in(const void* low, const void* high)
{
	gc_os_lock();
	GCState* gc = collector();
	const uint64_t dropped = gc_finalize_drop_in(&gc->finalizers, &gc->ready, low, high);
	gc_os_unlock();
	return dropped;
}

void c4_gc_register_root_enumerator(void* key, c4_gc_enum_roots_fn fn, void* user_data)
{
	if (!key || !fn) return;
	gc_os_lock();
	GCEntry* entry = gc_map_upsert(&collector()->enumerators, key);
	entry->a = (void*)fn;
	entry->b = user_data;
	gc_os_unlock();
}

void c4_gc_unregister_root_enumerator(void* key)
{
	gc_os_lock();
	gc_map_remove(&collector()->enumerators, key);
	gc_os_unlock();
}

void c4_gc_register_collection_result(void* key, c4_gc_on_collection_fn fn, void* user_data)
{
	if (!key || !fn) return;
	gc_os_lock();
	GCEntry* entry = gc_map_upsert(&collector()->results, key);
	entry->a = (void*)fn;
	entry->b = user_data;
	gc_os_unlock();
}

void c4_gc_unregister_collection_result(void* key)
{
	gc_os_lock();
	gc_map_remove(&collector()->results, key);
	gc_os_unlock();
}

void c4_gc_pin(void* obj) { gc_os_lock(); gc_pin(collector(), obj); gc_os_unlock(); }
void c4_gc_unpin(void* obj) { gc_os_lock(); gc_unpin(collector(), obj); gc_os_unlock(); }

bool c4_gc_is_reachable(const void* ptr)
{
	gc_os_lock();
	const bool reachable = gc_heap_find(&collector()->heap, ptr).span != NULL;
	gc_os_unlock();
	return reachable;
}

// ------------------------------------------------------------------------------------------------
// Statistics

// A thread sees its own allocations in the counts; another thread's reach them at its next refill or collection.
static c4_gc_stats locked_stats(void)
{
	GCThread* self = this_thread();
	gc_os_lock();
	GCState* gc = collector();
	gc->heap.liveBytes += (int64_t)self->cache.allocatedBytes;
	gc->allocs += self->cache.allocs;
	self->cache.allocatedBytes = 0;
	self->cache.allocs = 0;
	const c4_gc_stats s = gc_stats(gc);
	gc_os_unlock();
	return s;
}

c4_gc_stats c4_gc_get_stats(void) { return locked_stats(); }
uint64_t c4_gc_allocs(void) { return locked_stats().allocs; }
uint64_t c4_gc_collections(void) { return locked_stats().collections; }
uint64_t c4_gc_heap_bytes(void) { return locked_stats().heapBytes; }
uint64_t c4_gc_free_bytes(void) { return locked_stats().freeBytes; }

size_t c4_gc_region_count(void)
{
	gc_os_lock();
	const GCState* gc = collector();
	const size_t count = gc->regions.count + gc->enumerators.count + gc->results.count;
	gc_os_unlock();
	return count;
}

void c4_gc_set_stats_printer(void (*fn)(const c4_gc_stats*)) { gc_os_lock(); collector()->statsPrinter = fn; gc_os_unlock(); }

static void print_stats(const c4_gc_stats* s)
{
	fprintf(stderr, "[GC] allocs=%llu collections=%llu heapBytes=%llu liveBytes=%llu freeBytes=%llu pauseNs=%llu lastPauseNs=%llu\n",
		(unsigned long long)s->allocs, (unsigned long long)s->collections, (unsigned long long)s->heapBytes,
		(unsigned long long)s->liveBytes, (unsigned long long)s->freeBytes, (unsigned long long)s->pauseNs,
		(unsigned long long)s->lastPauseNs);
}

void c4_gc_install_default_stats_printer(void) { c4_gc_set_stats_printer(print_stats); }
