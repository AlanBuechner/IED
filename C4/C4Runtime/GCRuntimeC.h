#pragma once
// C4 garbage collector: conservative, non-moving, stop-the-world mark-sweep (docs/runtime/01-memory-and-gc.md,
// D-47). Every `T*` is a managed pointer. Roots are the globals the compiler lists in __c4_gc_roots, the
// calling thread's registers and stack, registered scan regions, root enumerators, pinned objects and objects
// waiting for their finalizer. Thread-safe: every entry point takes the collector's lock (D-48).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Allocation ---------------------------------------------------------------------------------
// Zeroed managed storage aligned to `align` (a power of two, at most 8192; 16 or more in practice). Never
// returns NULL: running out of memory is a fatal error.
void* c4_alloc(size_t size, size_t align);

// The same for an object that holds no pointers (the compiler uses it for pointer-free types): the
// collector never scans it, so a pointer stored in it as an integer does not keep anything alive.
void* c4_alloc_noscan(size_t size, size_t align);

// Ends an object now (`delete p`): runs its finalizer, then frees its memory. `ptr` may point anywhere
// inside the object; NULL and pointers outside the heap are ignored.
void c4_free(void* ptr);

// Runs `fn(object)` once when the collector reclaims the object (or when it is freed with c4_free).
void c4_gc_register_finalizer(void* obj, void (*fn)(void*));

// --- Collection ---------------------------------------------------------------------------------
// Collects now, also while automatic collection is disabled.
void c4_gc_collect(void);

// Every thread that runs C4 code is known to the collector, which stops it during a collection and scans its stack
// and thread_local values (D-48). The main thread and threads Bedrock starts register themselves, and a thread
// registers when it first allocates; any other thread calls this before C4 code holds pointers on it. A registered
// thread is forgotten when it ends.
void c4_gc_register_thread(void);

// Automatic collection runs when the live heap has grown by `percent` since the last collection (default
// 100, at least 4 MiB). Returns the previous value; values below 1 are treated as 1.
int32_t c4_gc_set_percent(int32_t percent);
// The threads that mark during a collection of a heap past 8 MiB, the collecting one included: -1 (the default)
// chooses by the processors (up to 8), 1 or 0 marks on the collecting thread alone. Returns the previous value.
int32_t c4_gc_set_mark_threads(int32_t threads);

// Automatic collections of a live heap of at least `bytes` mark concurrently (D-79): two short stops of the program,
// the marking itself beside it. -1: never; 0: always; default 16 MiB. A collection the program asks for (c4_gc_collect)
// stops the world, after finishing a concurrent one that runs. Returns the previous value. The environment's
// C4_GC_CONCURRENT ("always", "never" or bytes) sets it at startup.
int64_t c4_gc_set_concurrent(int64_t bytes);
// Checks every concurrent cycle's marks against a fresh mark from the roots, a fatal error naming any object it missed
// (tests and diagnosis; slow). C4_GC_VERIFY=1 sets it at startup.
void c4_gc_set_verify(bool on);

// Suppress / resume automatic collection (not counted: one enable resumes it).
void c4_gc_disable(void);
void c4_gc_enable(void);

// --- Roots --------------------------------------------------------------------------------------
// Memory outside the managed heap (raw buffers) that may hold managed pointers: every aligned word of a
// registered region is scanned. Registering the same start again replaces its size.
void c4_gc_register_region(const void* start, size_t size);
void c4_gc_unregister_region(const void* start);

// Storage that holds handles instead of pointers: once per collection `fn` reports each live object by
// calling visit(visit_ctx, ptr) (any pointer into the object). `key` identifies the registration.
typedef void (*c4_gc_visit_root_fn)(void* visit_ctx, void* ptr);
typedef void (*c4_gc_enum_roots_fn)(void* user_data, c4_gc_visit_root_fn visit, void* visit_ctx);
void c4_gc_register_root_enumerator(void* key, c4_gc_enum_roots_fn fn, void* user_data);
void c4_gc_unregister_root_enumerator(void* key);

// Keeps an object (and what it refers to) alive while native code holds it. Counted: each pin needs an unpin.
// Any pointer into the object works.
void c4_gc_pin(void* obj);
void c4_gc_unpin(void* obj);

// --- Pools ---------------------------------------------------------------------------------------
// Memory a custom allocator divides into objects of one size, which the collector tracks one by one (D-78). The
// allocator occupies and vacates slots; a collection marks occupied objects like any other, and vacates each one
// nothing reached, handing it to `release` after the collection (without the lock, one at a time) - unless
// `keep_all` keeps every occupied object, or `keep` keeps that one. `keep` runs during the collection with every
// other thread stopped, so it must not take a lock another thread may hold, nor occupy slots. Slot i is at
// c4_gc_pool_base(pool) + i * c4_gc_pool_slot_size(pool) (`slot_size` rounded up to 16, at most 1 MiB); the memory
// starts zeroed and is the pool's until it is destroyed. `delete` and finalizers do not apply to pool objects.
typedef bool (*c4_gc_pool_keep_fn)(void* user_data, void* object);
typedef void (*c4_gc_pool_release_fn)(void* user_data, void* object);
typedef struct c4_gc_pool_desc
{
	size_t slot_size;
	size_t slot_count;
	bool noscan;                       // the objects hold no managed pointers: marking does not scan them
	bool keep_all;                     // every occupied object stays alive, scanned, until it is vacated
	c4_gc_pool_keep_fn keep;           // optional: asked for each occupied object nothing reached
	c4_gc_pool_release_fn release;     // optional: told of each object a collection vacated
	void* user_data;
} c4_gc_pool_desc;
typedef struct c4_gc_pool c4_gc_pool;
// NULL when the sizes are out of range or memory ran out.
c4_gc_pool* c4_gc_pool_create(const c4_gc_pool_desc* desc);
// Its objects end with it; releases still queued for it are dropped.
void c4_gc_pool_destroy(c4_gc_pool* pool);
void* c4_gc_pool_base(const c4_gc_pool* pool);
size_t c4_gc_pool_slot_size(const c4_gc_pool* pool);
size_t c4_gc_pool_slot_count(const c4_gc_pool* pool);
// False when `object` is not the start of one of the pool's slots.
bool c4_gc_pool_occupy(c4_gc_pool* pool, void* object);
bool c4_gc_pool_vacate(c4_gc_pool* pool, void* object);
bool c4_gc_pool_occupied(const c4_gc_pool* pool, const void* object);

// --- After a collection ---------------------------------------------------------------------------
// Callbacks run after each collection's sweep. There, c4_gc_is_reachable tells whether an object survived.
typedef void (*c4_gc_on_collection_fn)(void* user_data);
void c4_gc_register_collection_result(void* key, c4_gc_on_collection_fn fn, void* user_data);
void c4_gc_unregister_collection_result(void* key);

// Whether `ptr` points into a live object: one that survived the last collection or was allocated since.
bool c4_gc_is_reachable(const void* ptr);

// --- Statistics ---------------------------------------------------------------------------------
typedef struct c4_gc_stats
{
	uint64_t allocs;          // allocations so far
	uint64_t collections;     // collections so far
	uint64_t heapBytes;       // memory the heap holds from the OS
	uint64_t freeBytes;       // heap memory not holding objects
	uint64_t pauseNs;         // total collection time
	uint64_t liveBytes;       // bytes of live objects (slot sizes)
	uint64_t lastPauseNs;     // the last collection's time
} c4_gc_stats;

c4_gc_stats c4_gc_get_stats(void);

// One stop of the program by a collection, in nanoseconds of the system's high-resolution counter
// (QueryPerformanceCounter scaled to nanoseconds).
typedef struct c4_gc_pause
{
	uint64_t startNs;
	uint64_t endNs;
} c4_gc_pause;
// The stops of the last collection into `out` (room for `capacity`), oldest first: how many there were. One for a
// stop-the-world collection; a concurrent cycle stops the program to copy its roots, again for each pass that copies
// them anew, and once to finish.
int32_t c4_gc_last_pauses(c4_gc_pause* out, int32_t capacity);
uint64_t c4_gc_allocs(void);
uint64_t c4_gc_collections(void);
uint64_t c4_gc_heap_bytes(void);
uint64_t c4_gc_free_bytes(void);

// Registered scan regions + root enumerators + collection-result callbacks.
size_t c4_gc_region_count(void);

// Called after each automatic collection (`-gc-stats` installs the default printer).
void c4_gc_set_stats_printer(void (*fn)(const c4_gc_stats*));
void c4_gc_install_default_stats_printer(void);

// --- Unmanaged memory (C4Runtime/MemoryRuntimeC.c) ----------------------------------------------------
// Never scanned and never freed by the collector.
void* c4_raw_alloc(size_t size);
void c4_raw_free(void* ptr);
// Unmanaged memory aligned to `align` (a power of two), for over-aligned elements; freed with c4_raw_free_aligned.
void* c4_raw_alloc_aligned(size_t size, size_t align);
void c4_raw_free_aligned(void* ptr);
// A buffer that grows (a StringBuilder's text, an Array's elements): heap memory while small; from 1 MiB up, address
// space reserved for growth whose pages are committed as it grows, so growing moves nothing until the reservation is
// outgrown. The owner passes back the size it asked for last, which alone says which kind of memory the buffer is.
// `buffer` (null with `oldBytes` 0) resized to `newBytes` with its first `keepBytes` kept; null when `newBytes` is 0,
// and null with `buffer` untouched when memory runs out. Aligned to 16.
void* c4_buffer_resize(void* buffer, size_t oldBytes, size_t keepBytes, size_t newBytes);
void c4_buffer_free(void* buffer, size_t bytes);
// The same for a buffer the collector scans for pointers (an Array's elements): a scan region of its whole size,
// never left registered over memory it gives back, and the elements always in a registered region as they move.
void* c4_buffer_resize_scanned(void* buffer, size_t oldBytes, size_t keepBytes, size_t newBytes);
void c4_buffer_free_scanned(void* buffer, size_t bytes);

// --- Compiler interface ---------------------------------------------------------------------------
// Every global, static member and static local that may hold a managed pointer, as {address, size};
// terminated by {NULL, 0}. Emitted into every executable.
typedef struct c4_gc_root_range
{
	void* start;
	uint64_t size;
} c4_gc_root_range;

extern const c4_gc_root_range __c4_gc_roots[];

// Another image's table of the same form (a script's, docs/runtime/04): its ranges are roots until it is
// removed, and it must stay as it is meanwhile. Adding a table twice adds it once.
void c4_gc_add_root_table(const c4_gc_root_range* table);
void c4_gc_remove_root_table(const c4_gc_root_range* table);

// Drops the finalizer of every object whose finalizer lies in [low, high) - code about to be unloaded, a
// script's - registered or waiting to run; the objects themselves stay. Returns how many it dropped.
uint64_t c4_gc_drop_finalizers_in(const void* low, const void* high);

#ifdef __cplusplus
}
#endif
