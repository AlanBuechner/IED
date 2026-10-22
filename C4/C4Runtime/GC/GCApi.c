// The exported collector API: thin entry points over the one collector of the process (the C API carries
// no context parameter, so this file owns the state). Each entry point takes the collector's lock; a small
// allocation first tries the calling thread's cache, which needs none. Finalizers and collection callbacks run
// after the lock is released, so they may block, allocate or start threads.

#include "GC/GCCollector.h"
#include "ThreadRuntimeC.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GCState g_gc;
static volatile int64_t g_nativeInUse;    // native memory threads not registered took and gave back; apart from g_gc,
                                          // which starts later
static int g_ready;
static _Thread_local GCThread* t_self;

static int start_collector_thread(void);

// C4_GC_CONCURRENT: "always", "never" or the live bytes from which automatic collections mark concurrently;
// C4_GC_VERIFY=1 checks every concurrent mark (D-79); C4_GC_TRACE=1 prints a line about each concurrent cycle;
// C4_GC_MIN_TRIGGER: the fewest live bytes that start an automatic collection (default 4 MiB), so small test programs
// collect often. For tests and diagnosis.
static void configure_from_environment(GCState* gc)
{
	const char* trigger = getenv("C4_GC_MIN_TRIGGER");
	if (trigger && strtoll(trigger, NULL, 10) > 0) gc->minTrigger = gc->nextCollection = (size_t)strtoll(trigger, NULL, 10);
	const char* concurrent = getenv("C4_GC_CONCURRENT");
	if (concurrent && strcmp(concurrent, "always") == 0) gc->concurrent.threshold = 0;
	else if (concurrent && strcmp(concurrent, "never") == 0) gc->concurrent.threshold = -1;
	else if (concurrent) gc->concurrent.threshold = (int64_t)strtoll(concurrent, NULL, 10);
	const char* verify = getenv("C4_GC_VERIFY");
	gc->concurrent.verify = verify && verify[0] == '1';
	const char* trace = getenv("C4_GC_TRACE");
	gc->concurrent.trace = trace && trace[0] == '1';
}

// The lock is held.
static GCState* collector(void)
{
	if (!g_ready)
	{
		gc_state_init(&g_gc, __c4_gc_roots);
		g_gc.startCollectorThread = start_collector_thread;
		configure_from_environment(&g_gc);
		g_ready = 1;
	}
	return &g_gc;
}

// Without the lock: the collector, made the first time.
static GCState* ready_collector(void)
{
	gc_os_lock();
	GCState* gc = collector();
	gc_os_unlock();
	return gc;
}

// The thread's C4 life ends: its thread_local objects first (they may still allocate), then its registration.
static void on_thread_exit(void* data)
{
	c4_thread_run_exits();
	GCThread* thread = (GCThread*)data;
	gc_os_lock();
	GCState* gc = collector();
	gc_thread_flush_cache(thread, &gc->heap, &gc->allocs);
	gc->ended.objects += thread->allocated.objects;
	gc->ended.bytes += thread->allocated.bytes;
	gc->ended.nativeBytes += thread->allocated.nativeBytes;
	gc->endedNativeInUse += thread->nativeInUse;
	gc_threads_remove(&gc->threads, thread);
	gc_os_unlock();
	t_self = NULL;
}

__attribute__((noinline)) static GCThread* register_this_thread(void)
{
	gc_os_lock();
	t_self = gc_threads_add(&collector()->threads);
	gc_os_unlock();
	gc_os_on_thread_exit(on_thread_exit, t_self);
	return t_self;
}

// The calling thread, registered with the collector the first time it asks.
static inline GCThread* this_thread(void)
{
	GCThread* self = t_self;
	return self ? self : register_this_thread();
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

// One thread at a time hands vacated pool objects to their pools; a pool destroyed meanwhile has its own dropped.
static void run_releases(GCState* gc)
{
	gc_os_lock();
	const int busy = gc->runningReleases;
	gc->runningReleases = 1;
	gc_os_unlock();
	if (busy) return;
	for (;;)
	{
		GCPoolRelease release;
		gc_os_lock();
		const int ready = gc_release_pop(&gc->releases, &release);
		const c4_gc_pool_desc desc = ready ? release.pool->desc : (c4_gc_pool_desc){ 0 };
		if (!ready) gc->runningReleases = 0;
		gc_os_unlock();
		if (!ready) return;
		desc.release(desc.user_data, release.object);
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
	run_releases(gc);
	run_finalizers(gc);
}

// ------------------------------------------------------------------------------------------------
// Objects

// A concurrent cycle that the program's allocation outruns is finished at once, so the heap stays within twice the
// size that started it.
static int cycle_outrun(const GCState* gc)
{
	return gc_concurrent_busy(gc) && gc_heap_live_bytes(&gc->heap) >= gc->nextCollection * 2;
}

__attribute__((noinline)) static void sample(GCThread* self, uint64_t bytes, const void* caller)
{
	GCState* gc = collector();
	c4_gc_alloc_sample record;
	record.bytes = bytes;
	record.timeNs = gc_os_now_ns();
	record.threadId = self->os.id;
	record.frameCount = gc_os_capture_stack(record.frames, C4_GC_SAMPLE_FRAMES, caller);
	gc_os_lock();
	gc_sampling_push(&gc->sampling, &record);
	gc_os_unlock();
}

// The sampler's countdown ran out: a sample when its turn has come, and the next countdown.
__attribute__((noinline)) static void check_sampler(GCThread* self, uint64_t bytes, const void* caller)
{
	const uint64_t seed = (uint64_t)(uintptr_t)self ^ gc_os_now_ns();
	if (gc_sampler_due(&self->sampler, &collector()->sampling, bytes, seed)) sample(self, bytes, caller);
}

// Counts an allocation `caller` made on `self`, and samples it when its turn has come.
static inline void note_allocation(GCThread* self, uint64_t bytes, const void* caller)
{
	self->allocated.objects += 1;
	self->allocated.bytes += bytes;
	self->sampler.untilCheck -= (int64_t)bytes;
	if (self->sampler.untilCheck < 0) check_sampler(self, bytes, caller);
}

// What the thread's cache cannot give: a large object, a new span, a collection.
__attribute__((noinline)) static void* allocate_slow(GCThread* self, GCAllocRequest request)
{
	for (;;)
	{
		gc_os_lock();
		GCState* gc = collector();
		void* p = gc_allocate(gc, self, request);
		const int outrun = cycle_outrun(gc);
		gc_os_unlock();
		if (p && !outrun)
		{
			after_collection();
			return p;
		}
		gc_concurrent_finish_now(gc);
		after_collection();
		if (p) return p;
	}
}

// The census's note of the object `self` just allocated: in its cache's span for a small one, found by address when the
// object came from elsewhere (a large one, or a span the slow path gave the cache).
__attribute__((noinline)) static void note_type(GCThread* self, GCAllocRequest request, void* p, const char* type)
{
	const int spanClass = gc_heap_small_class(request);
	GCSpan* span = spanClass >= 0 ? self->cache.spans[spanClass] : NULL;
	if (!span || (uint8_t*)p < span->base || (uint8_t*)p >= span->base + gc_span_bytes(span))
		span = gc_pages_span_of(&g_gc.heap.pages, (uintptr_t)p);
	gc_census_note(&g_gc.census, span, p, type);
}

// Inline into each entry point: the cache's slot, counted, with nothing called unless the cache is empty, a sample is
// due or a census runs.
static inline __attribute__((always_inline)) void* allocate(GCAllocRequest request, const void* caller, const char* type)
{
	GCThread* self = this_thread();
	const int spanClass = gc_heap_small_class(request);
	void* p = spanClass >= 0 ? gc_thread_alloc_fast(self, &g_gc.heap, spanClass) : NULL;
	if (!p) p = allocate_slow(self, request);
	note_allocation(self, request.size, caller);
	if (__atomic_load_n(&g_gc.census.on, __ATOMIC_RELAXED)) note_type(self, request, p, type);
	return p;
}

static GCAllocRequest request_of(size_t size, size_t align, int noscan)
{
	GCAllocRequest request = { size, align, noscan };
	return request;
}

void* c4_alloc(size_t size, size_t align) { return allocate(request_of(size, align, 0), __builtin_return_address(0), NULL); }
void* c4_alloc_noscan(size_t size, size_t align) { return allocate(request_of(size, align, 1), __builtin_return_address(0), NULL); }

void* c4_alloc_typed(size_t size, size_t align, const char* type)
{
	return allocate(request_of(size, align, 0), __builtin_return_address(0), type);
}

void* c4_alloc_typed_noscan(size_t size, size_t align, const char* type)
{
	return allocate(request_of(size, align, 1), __builtin_return_address(0), type);
}

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

// Ends a concurrent cycle under way. The collector's own thread (a finalizer it runs) never waits for itself: a cycle
// another thread asks for meanwhile starts once it is back.
static void settle_cycle(GCState* gc, GCThread* self)
{
	if (gc->concurrent.thread != self) gc_concurrent_finish_now(gc);
}

void c4_gc_collect(void)
{
	GCThread* self = this_thread();
	GCState* gc = ready_collector();
	for (;;)
	{
		settle_cycle(gc, self);
		after_collection();
		gc_os_lock();
		// another thread asked for a cycle meanwhile: finish that one too
		const int busy = gc->concurrent.thread != self && gc_concurrent_busy(gc);
		if (!busy) gc_collect(gc, self);
		gc_os_unlock();
		if (!busy) break;
	}
	after_collection();
}

int64_t c4_gc_set_concurrent(int64_t bytes)
{
	gc_os_lock();
	GCState* gc = collector();
	const int64_t previous = gc->concurrent.threshold;
	gc->concurrent.threshold = bytes < -1 ? -1 : bytes;
	gc_os_unlock();
	return previous;
}

void c4_gc_set_verify(bool on)
{
	gc_os_lock();
	collector()->concurrent.verify = on ? 1 : 0;
	gc_os_unlock();
}

// The thread that runs concurrent cycles: registered, so the C4 code a cycle runs (root enumerators, pool keep
// callbacks) may allocate; after each cycle it runs the callbacks, releases and finalizers itself.
static void collector_thread(void* unused)
{
	(void)unused;
	GCThread* self = this_thread();
	for (;;)
	{
		gc_concurrent_serve(&g_gc, self);
		after_collection();
	}
}

static int start_collector_thread(void) { return gc_os_start_helper(collector_thread, NULL); }

// A concurrent cycle under way finishes first (BUG-290), so none completes while collection is disabled.
void c4_gc_disable(void)
{
	GCThread* self = this_thread();
	GCState* gc = ready_collector();
	gc_os_lock();
	gc->disabled = 1;
	gc_os_unlock();
	settle_cycle(gc, self);
}
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

// ------------------------------------------------------------------------------------------------
// Pools

c4_gc_pool* c4_gc_pool_create(const c4_gc_pool_desc* desc)
{
	if (!desc) return NULL;
	gc_os_lock();
	GCState* gc = collector();
	GCPool* pool = gc_pool_create(&gc->heap, &gc->pools, desc);
	gc_os_unlock();
	return (c4_gc_pool*)pool;
}

void c4_gc_pool_destroy(c4_gc_pool* pool)
{
	if (!pool) return;
	gc_os_lock();
	GCState* gc = collector();
	gc_pool_destroy(&gc->heap, &gc->pools, (GCPool*)pool, &gc->releases, gc->collections);
	gc_os_unlock();
}

void* c4_gc_pool_base(const c4_gc_pool* pool) { return pool ? ((const GCPool*)pool)->base : NULL; }
size_t c4_gc_pool_slot_size(const c4_gc_pool* pool) { return pool ? ((const GCPool*)pool)->desc.slot_size : 0; }
size_t c4_gc_pool_slot_count(const c4_gc_pool* pool) { return pool ? ((const GCPool*)pool)->desc.slot_count : 0; }

// Whether the slot was occupied before (-1: `object` starts no slot of the pool).
static int set_occupied(c4_gc_pool* pool, void* object, int occupied)
{
	const int64_t slot = pool ? gc_pool_slot_of((const GCPool*)pool, object) : -1;
	if (slot < 0) return -1;
	gc_os_lock();
	const int was = gc_pool_occupied((const GCPool*)pool, (size_t)slot) != 0;
	gc_pool_set_occupied((GCPool*)pool, (size_t)slot, occupied, __atomic_load_n(&collector()->heap.allocateMarked, __ATOMIC_ACQUIRE));
	gc_os_unlock();
	return was;
}

// Occupying a vacant slot is an allocation of the slot's size.
bool c4_gc_pool_occupy(c4_gc_pool* pool, void* object)
{
	GCThread* self = this_thread();
	const int was = set_occupied(pool, object, 1);
	if (was == 0) note_allocation(self, c4_gc_pool_slot_size(pool), __builtin_return_address(0));
	return was >= 0;
}

bool c4_gc_pool_vacate(c4_gc_pool* pool, void* object) { return set_occupied(pool, object, 0) >= 0; }

bool c4_gc_pool_occupied(const c4_gc_pool* pool, const void* object)
{
	const int64_t slot = pool ? gc_pool_slot_of((const GCPool*)pool, object) : -1;
	if (slot < 0) return false;
	gc_os_lock();
	const bool occupied = gc_pool_occupied((const GCPool*)pool, (size_t)slot) != 0;
	gc_os_unlock();
	return occupied;
}

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

// Every thread's native bytes in use, the ended ones' and the unregistered ones' (under the lock; a thread's own count
// may lag the read by its last allocation).
static int64_t native_in_use(const GCState* gc)
{
	int64_t total = __atomic_load_n(&g_nativeInUse, __ATOMIC_RELAXED) + gc->endedNativeInUse;
	for (const GCThread* thread = gc->threads.all; thread; thread = thread->next) total += thread->nativeInUse;
	return total;
}

c4_gc_stats c4_gc_get_stats(void)
{
	gc_os_lock();
	const GCState* gc = collector();
	c4_gc_stats stats = gc_stats(gc);
	const int64_t native = native_in_use(gc);
	gc_os_unlock();
	stats.nativeBytes = native > 0 ? (uint64_t)native : 0;
	return stats;
}

// A registered thread counts on its own record, without an atomic: native memory comes and goes often.
void c4_gc_native_taken(uint64_t bytes)
{
	GCThread* self = t_self;
	if (!self)
	{
		__atomic_add_fetch(&g_nativeInUse, (int64_t)bytes, __ATOMIC_RELAXED);
		return;
	}
	self->allocated.nativeBytes += bytes;
	self->nativeInUse += (int64_t)bytes;
}

void c4_gc_native_given(uint64_t bytes)
{
	GCThread* self = t_self;
	if (self) self->nativeInUse -= (int64_t)bytes;
	else __atomic_sub_fetch(&g_nativeInUse, (int64_t)bytes, __ATOMIC_RELAXED);
}

c4_gc_alloc_counts c4_gc_thread_allocated(void) { return this_thread()->allocated; }

uint64_t c4_gc_set_alloc_sampling(uint64_t everyBytes)
{
	GCThread* self = this_thread();
	gc_os_lock();
	const uint64_t previous = gc_sampling_set(&collector()->sampling, everyBytes);
	gc_os_unlock();
	self->sampler.untilCheck = 0;
	self->sampler.checkSpan = 0;
	return previous;
}

bool c4_gc_set_census(bool on)
{
	gc_os_lock();
	const bool was = gc_census_set(&collector()->census, on);
	gc_os_unlock();
	return was;
}

int32_t c4_gc_last_census(c4_gc_census_entry* out, int32_t capacity)
{
	gc_os_lock();
	const int32_t count = gc_census_last(&collector()->census, out, capacity);
	gc_os_unlock();
	return count;
}

int32_t c4_gc_take_alloc_samples(c4_gc_alloc_sample* out, int32_t capacity)
{
	gc_os_lock();
	const int32_t taken = gc_sampling_take(&collector()->sampling, out, capacity);
	gc_os_unlock();
	return taken;
}

c4_gc_alloc_counts c4_gc_allocated(void)
{
	gc_os_lock();
	const GCState* gc = collector();
	c4_gc_alloc_counts total = gc->ended;
	for (const GCThread* thread = gc->threads.all; thread; thread = thread->next)
	{
		total.objects += thread->allocated.objects;
		total.bytes += thread->allocated.bytes;
		total.nativeBytes += thread->allocated.nativeBytes;
	}
	gc_os_unlock();
	return total;
}

int32_t c4_gc_last_pauses(c4_gc_pause* out, int32_t capacity)
{
	gc_os_lock();
	const GCStops stops = collector()->lastStops;
	gc_os_unlock();
	for (int32_t i = 0; out && i < capacity && i < (int32_t)stops.count; ++i) out[i] = stops.stops[i];
	return (int32_t)stops.count;
}
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
