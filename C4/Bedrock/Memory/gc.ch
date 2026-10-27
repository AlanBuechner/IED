// The garbage collector (docs/runtime/01-memory-and-gc.md): collection control, statistics, pins, scan
// regions and the hooks an engine uses for handle-based storage. Every function is the C runtime's
// c4_gc_* entry point of the same meaning.

namespace GC
{
	struct Stats
	{
		public uint64 allocs;        // allocations so far
		public uint64 collections;   // collections so far
		public uint64 heapBytes;     // memory the heap holds from the OS
		public uint64 freeBytes;     // heap memory not holding objects
		public uint64 pauseNs;       // total collection time
		public uint64 liveBytes;     // bytes of live objects
		public uint64 lastPauseNs;   // the last collection's time
		public uint64 nativeBytes;   // native memory in use: raw_alloc's blocks and container buffers (Array, String, ...)
	}

	// visit(visitContext, object) reports one live object; an enumerator gets it with its user data.
	alias VisitRoot = funcptr<void(void*, void*)>;
	alias RootEnumerator = funcptr<void(void*, VisitRoot, void*)>;
	alias CollectionCallback = funcptr<void(void*)>;

	[link("c4_gc_collect")] void Collect() noexcept;
	[link("c4_gc_get_stats")] Stats GetStats() noexcept;

	// Allocations counted as they are made: objects, and the bytes asked for (a slot's size when a Pool hands one out);
	// native memory taken: raw_alloc's blocks and the bytes container buffers (Array, String, ...) grew by; and native
	// memory given back, by the same measure, whichever thread took it.
	struct AllocCounts
	{
		public uint64 objects;
		public uint64 bytes;
		public uint64 nativeBytes;
		public uint64 nativeFreed;
	}
	// The calling thread's allocations since it started, never reset; reads the thread's own counters, no lock, so a
	// profiler can take it around any scope.
	[link("c4_gc_thread_allocated")] AllocCounts ThreadAllocated() noexcept;
	// Every thread's allocations so far, those of threads that ended included (takes the collector's lock).
	[link("c4_gc_allocated")] AllocCounts Allocated() noexcept;

	// One sampled allocation (SetAllocSampling).
	struct AllocSample
	{
		public uint64 bytes;        // the allocation's size
		public uint64 timeNs;       // when, on LastPauses' clock
		public uint64 threadId;     // the allocating thread's OS id
		public int32 frameCount;
		public void*[32] frames;    // return addresses, the allocating function's first
	}
	// About one allocation in every `everyBytes` bytes a thread allocates is recorded with its call stack, at points
	// drawn at random so that each sample stands for `everyBytes` bytes on average; 0 (the default) turns it off.
	// Returns the previous setting. The calling thread takes a change up at once, another within 64 KiB.
	[link("c4_gc_set_alloc_sampling")] uint64 SetAllocSampling(uint64 everyBytes) noexcept;
	// Moves up to `capacity` of the oldest waiting samples to `out`: how many. The latest 4096 are kept.
	[link("c4_gc_take_alloc_samples")] int32 TakeAllocSamples(AllocSample* out, int32 capacity) noexcept;

	// The live objects of one type in a census (SetCensus).
	struct CensusEntry
	{
		public const char* type;    // its qualified name (Reflection's), `T[]` for an array of T; nullptr: not known
		public uint64 objects;
		public uint64 bytes;        // the slots they occupy
		public uint64 nativeBytes;  // the native memory they hold: native_bytes_of each (docs/language/14)
	}
	// While on, every `new` notes its type, and each collection counts the live objects by it and the native memory they
	// hold (LastCensus); objects made before it was on, by native code or in a Pool are of no known type. It costs 8
	// bytes beside each slot of the spans allocated from and a walk of the live objects at each collection, with
	// native_bytes_of each one whose type holds native memory. Those run with every other thread stopped wherever it
	// was, so a [operator("native_bytes")] only reads; one that faults counts 0. Returns whether it was on.
	[link("c4_gc_set_census")] bool SetCensus(bool on) noexcept;
	// The last census (taken by the last collection while it was on) into `out` (room for `capacity`), one entry a
	// type name, by name, the unknown first: how many entries it has. Read it in a collection-result callback.
	[link("c4_gc_last_census")] int32 LastCensus(CensusEntry* out, int32 capacity) noexcept;

	// One stop of the program by a collection, in nanoseconds of the system's high-resolution counter
	// (Platform::Ticks() scaled by 1e9 / Platform::TickFrequency()).
	struct Pause
	{
		public uint64 startNs;
		public uint64 endNs;
	}
	// The stops of the last collection into `out` (room for `capacity`), oldest first: how many there were. One for
	// a stop-the-world collection; a concurrent cycle stops the program to copy its roots, again for each pass that
	// copies them anew, and once to finish.
	[link("c4_gc_last_pauses")] int32 LastPauses(Pause* out, int32 capacity) noexcept;

	// Automatic collection starts when the live heap has grown by `percent` since the last collection
	// (default 100, at least 4 MiB). Returns the previous value.
	[link("c4_gc_set_percent")] int32 SetPercent(int32 percent) noexcept;

	// The threads that mark a heap past 8 MiB, the collecting one included: -1 (default) picks by the processors,
	// up to 8; 1 marks alone. Returns the previous value.
	[link("c4_gc_set_mark_threads")] int32 SetMarkThreads(int32 threads) noexcept;

	// Automatic collections of a live heap of at least `bytes` mark beside the program, with two short stops (D-79):
	// -1 never, 0 always, default 16 MiB. Collect() stops the world. Returns the previous value.
	[link("c4_gc_set_concurrent")] int64 SetConcurrent(int64 bytes) noexcept;
	// Checks every concurrent mark against a fresh mark from the roots; a missed object is a fatal error (tests; slow).
	[link("c4_gc_set_verify")] void SetVerify(bool on) noexcept;

	// Suppress / resume automatic collection (not counted). Collect() still works while disabled. Disable finishes a
	// concurrent cycle under way first, so no automatic collection completes until Enable.
	[link("c4_gc_disable")] void Disable() noexcept;
	[link("c4_gc_enable")] void Enable() noexcept;

	// Keep an object alive while native code holds it (counted: one Unpin per Pin). Any pointer into it works.
	[link("c4_gc_pin")] void Pin(void* object) noexcept;
	[link("c4_gc_unpin")] void Unpin(void* object) noexcept;

	// Whether `object` points into a live object; meaningful in a collection callback.
	[link("c4_gc_is_reachable")] bool IsReachable(void* object) noexcept;

	// Raw memory that may hold managed pointers.
	[link("c4_gc_register_region")] void RegisterRegion(void* start, uint64 size) noexcept;
	[link("c4_gc_unregister_region")] void UnregisterRegion(void* start) noexcept;
	// How many regions are registered: containers register a buffer only when its elements may hold pointers.
	[link("c4_gc_region_count")] uint64 RegionCount() noexcept;

	[link("c4_gc_register_root_enumerator")] void RegisterRootEnumerator(void* key, RootEnumerator enumerate, void* userData) noexcept;
	[link("c4_gc_unregister_root_enumerator")] void UnregisterRootEnumerator(void* key) noexcept;

	[link("c4_gc_register_collection_result")] void RegisterCollectionResult(void* key, CollectionCallback callback, void* userData) noexcept;
	[link("c4_gc_unregister_collection_result")] void UnregisterCollectionResult(void* key) noexcept;
}
