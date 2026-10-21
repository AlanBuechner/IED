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
	}

	// visit(visitContext, object) reports one live object; an enumerator gets it with its user data.
	alias VisitRoot = funcptr<void(void*, void*)>;
	alias RootEnumerator = funcptr<void(void*, VisitRoot, void*)>;
	alias CollectionCallback = funcptr<void(void*)>;

	[link("c4_gc_collect")] void Collect();
	[link("c4_gc_get_stats")] Stats GetStats();

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
	[link("c4_gc_last_pauses")] int32 LastPauses(Pause* out, int32 capacity);

	// Automatic collection starts when the live heap has grown by `percent` since the last collection
	// (default 100, at least 4 MiB). Returns the previous value.
	[link("c4_gc_set_percent")] int32 SetPercent(int32 percent);

	// The threads that mark a heap past 8 MiB, the collecting one included: -1 (default) picks by the processors,
	// up to 8; 1 marks alone. Returns the previous value.
	[link("c4_gc_set_mark_threads")] int32 SetMarkThreads(int32 threads);

	// Automatic collections of a live heap of at least `bytes` mark beside the program, with two short stops (D-79):
	// -1 never, 0 always, default 16 MiB. Collect() stops the world. Returns the previous value.
	[link("c4_gc_set_concurrent")] int64 SetConcurrent(int64 bytes);
	// Checks every concurrent mark against a fresh mark from the roots; a missed object is a fatal error (tests; slow).
	[link("c4_gc_set_verify")] void SetVerify(bool on);

	// Suppress / resume automatic collection (not counted). Collect() still works while disabled. Disable finishes a
	// concurrent cycle under way first, so no automatic collection completes until Enable.
	[link("c4_gc_disable")] void Disable();
	[link("c4_gc_enable")] void Enable();

	// Keep an object alive while native code holds it (counted: one Unpin per Pin). Any pointer into it works.
	[link("c4_gc_pin")] void Pin(void* object);
	[link("c4_gc_unpin")] void Unpin(void* object);

	// Whether `object` points into a live object; meaningful in a collection callback.
	[link("c4_gc_is_reachable")] bool IsReachable(void* object);

	// Raw memory that may hold managed pointers.
	[link("c4_gc_register_region")] void RegisterRegion(void* start, uint64 size);
	[link("c4_gc_unregister_region")] void UnregisterRegion(void* start);

	[link("c4_gc_register_root_enumerator")] void RegisterRootEnumerator(void* key, RootEnumerator enumerate, void* userData);
	[link("c4_gc_unregister_root_enumerator")] void UnregisterRootEnumerator(void* key);

	[link("c4_gc_register_collection_result")] void RegisterCollectionResult(void* key, CollectionCallback callback, void* userData);
	[link("c4_gc_unregister_collection_result")] void UnregisterCollectionResult(void* key);
}
