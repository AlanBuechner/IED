# Memory model and garbage collector

C4 is garbage collected. The design goal (AGENTS.md) is a Go-style collector: you allocate with
`new`, the runtime reclaims what is no longer reachable, and value-type objects on the stack get
deterministic RAII. Raw, unscanned memory is available when you need full control.

Runtime source: `C4Runtime/GCRuntimeC.h` (the C API) and `C4Runtime/GC/` (pure C plus one C++ file), linked
into every executable. Compiler side: allocation, finalizer registration and the global root table are
emitted by `C4Lib/src/Backend/CodeGen/` (`ObjectEmitter`, `GcRoots`). From C4, use the builtins below or the
Bedrock `GC` namespace (`include "Memory/gc.ch"`).

See also: [language/06-lifecycle-and-operators.md](../language/06-lifecycle-and-operators.md) for
`[operator("create")]` / `[operator("delete")]`, and [language/14-builtins.md](../language/14-builtins.md)
for the full builtin list.

NOTE: this GC api could use some more fleshing out to have a more complete and consistent API. also im not fully sure that we should have GC it is mainly here so we ca re use C4 as a scripting language with ORC2

---

## 1. Language-level model

| Rule | Status |
|---|---|
| Every pointer `T*` is a managed GC pointer. There is no separate raw pointer type. | **Implemented** |
| Objects are value types. A local `T v;` lives on the stack and is destroyed at scope exit (RAII). | **Implemented** |
| `new T`, `new T(args)`, `new T{...}`, `new int32` allocate on the managed heap (memory is zeroed). | **Implemented** |
| `[operator("delete")]` is the destructor. For value locals it runs at scope exit (also on early `return` and `throw`, LIFO). For heap objects it is registered as the GC finalizer and runs exactly once. | **Implemented** |
| `delete p` runs the finalizer immediately and frees the object; its memory is reused. `p` may point anywhere inside the object; `delete null` does nothing. The collector never runs that finalizer again. | **Implemented** |
| A pointer kept only in an integer does not keep its object alive (an object of a pointer-free type is never scanned). | **Implemented** |
| An exception escaping a finalizer stops the program (`C4: fatal: an exception escaped a finalizer`). | **Implemented** |
| `raw_alloc(size)` / `raw_free(p)` give unmanaged memory that the collector never scans or frees. | **Implemented** |

```c4
int32 g_finalized = 0;

struct Widget {
	int32 value;
	[operator("create")] void Init(int32 v) { value = v * 2; }
	[operator("delete")] void Finalize() { g_finalized++; }
};

void Example() {
	Widget* w = new Widget(21);   // heap, create runs, finalizer registered
	Widget local(3);              // stack, create runs now, delete runs at scope exit
	delete w;                     // finalizer runs now, exactly once
	gc_collect();                 // force a collection
}
```

Containers that keep `T*` inside raw memory must tell the collector about that memory. Bedrock's
`Core::Array<T>` does this:

```c4
data = (T*)raw_alloc((uint64)(capacity * (int64)sizeof(T)));
gc_register_region((void*)data, (uint64)(capacity * (int64)sizeof(T)));   // scan this buffer for pointers
...
gc_unregister_region((void*)data);                  // before raw_free
raw_free((void*)data);
```

### Finalizers

A finalizer (the `[operator("delete")]` of a heap object) runs after the collection that finds the object
unreachable, never in the middle of it:

- The object and everything it refers to stay alive until its finalizer has run; the memory is reclaimed by
  a later collection.
- If an unreachable finalizable object `A` refers to another one `B`, `A` is finalized first and `B` in a
  later collection, so a finalizer never sees an object whose finalizer already ran. Pointers from an
  object to itself do not hold it back, and a cycle of finalizable objects is finalized one object per
  collection.
- A finalizer may allocate and may trigger a collection.

---

## 2. The collector

**Implemented** - conservative, non-moving, stop-the-world mark-sweep (D-47) over every thread that runs C4
code (D-48). Windows x64.

| Aspect | Behaviour |
|---|---|
| Heap | 64 MiB arenas from the OS (huge objects get a run of contiguous arenas). A two-level map from address to arena gives the span of any 8 KiB page in O(1). |
| Objects | No headers. Objects up to 32 KiB live in slots of 72 size classes (16-byte steps to 256, then 8 steps per doubling; at most 1/8 of a slot wasted); larger ones, and alignments above 64, get pages of their own. A span's bitmaps record which slots hold objects and which are marked. Alignment: any power of two up to 8192. |
| Pointer-free objects | The compiler allocates objects of pointer-free types (`TypeHandle::MayHoldPointers`) with `c4_alloc_noscan`; they live in separate spans that marking never scans. |
| Roots | `__c4_gc_roots` (every global, static member and static local that may hold a pointer, as address ranges); every registered thread's registers and stack, and its copy of each thread_local that may hold a pointer (`__c4_gc_thread_roots`); registered scan regions; root enumerators; pinned objects; objects waiting for their finalizer. |
| Threads | A thread registers on its first allocation, through `c4_gc_register_thread()`, or when it attaches (`thread_start` attaches a thread before its entry runs, `thread_attach()` attaches any other); the main thread registers before global initialization. As a thread ends, its thread_local objects are destroyed before it unregisters. A collection holds the collector's lock, suspends every other registered thread (`SuspendThread`, then `GetThreadContext` so it has really stopped), scans each one's full register context and its stack from its stack pointer up, and resumes them after the sweep. A thread unregisters when it ends. A thread that never allocated and never registered is not scanned: it must not hold the only reference to an object. |
| Thread caches | Each thread allocates small objects from spans its cache owns, without the lock: claiming a slot is an atomic bit set, so a `delete` on another thread can free slots of an owned span meanwhile. A collection waits out a thread inside that fast path before scanning it, and hands every cached span back to the heap before the sweep. The lock is a re-entrant critical section; the collector's own memory comes from a private heap taken only under it, so no stopped thread can hold a lock the collection needs. |
| Mark | An explicit mark stack (no recursion). Every aligned word of a scanned range that points anywhere into a live object marks it. A heap past 8 MiB is marked in parallel: helper threads (started on first use, parked between collections, never registered or stopped) drain beside the collecting thread, each from its own stack, sharing work through a pool that busy markers fill while others are idle; a mark bit is claimed atomically, so each object is scanned once. Root ranges (globals, scan regions, other threads' stacks and thread_locals) are queued in 16 KiB chunks for every marker; the collecting thread's own stack and the stopped threads' registers are scanned on the spot. Root enumerators run first, alone. `c4_gc_set_mark_threads` sets the markers (default: the processors, up to 8; 1 marks alone). |
| Finalize | After marking: unreachable finalizable objects are kept with what they reach and queued (see section 1); their finalizers run after the collection, without the lock and one thread at a time, through `c4_gc_run_finalizer` (`GC/GCFinalizerShim.cpp`). Collection-result callbacks also run without the lock, so both may allocate, block or start threads. |
| Sweep | The marked slots become the allocated slots; spans left empty return their pages. Free pages coalesce; runs of at least 512 KiB that stayed free for a whole collection are returned to the OS and recommitted on reuse. |
| Trigger | Automatic, checked when an allocation leaves its thread's cache: a collection starts when the live bytes reach `live after the last collection x (1 + percent/100)`, at least 4 MiB. `percent` is 100 by default (`c4_gc_set_percent`). `c4_gc_disable` suppresses it; `gc_collect()` always works. Out of memory is a fatal error after one forced collection. |
| Write barrier | None: the stop-the-world collector rescans everything. A concurrent or generational collector would add one in codegen's store helper. |
| Stats | `c4_gc_get_stats()` returns `{ allocs, collections, heapBytes, freeBytes, pauseNs, liveBytes, lastPauseNs }`. `heapBytes` is the memory held from the OS, `freeBytes` the part of it not holding objects. A thread sees its own allocations in the counts at once; another thread's arrive when it next refills its cache or a collection runs. |

Source layout (`C4Runtime/GC/`): `GCPlatformWin32.c` (OS memory, the lock and private heap, thread suspension
and stack scans, thread exit, clock - the only platform file), `GCThreads` (the thread registry, per-thread
caches and their fast path), `GCPageHeap` (arenas, page map, free runs), `GCHeap` (size-class spans, allocation, release, sweep),
`GCMark` (mark stack, range scanning), `GCFinalize` (finalizer selection and queue), `GCRoots` (root
marking, pins), `GCPtrMap` (the hash map behind finalizers, pins, regions and hooks), `GCCollector` (the
collection cycle, pacing), `GCApi.c` (the exported functions; the only file with process state).

---

## 3. What C4 code can reach

The builtins are declared in the prelude (`Bedrock/Prelude/prelude.ch`) and need no include. The Bedrock `GC` namespace (`Bedrock/Memory/gc.ch`) covers the rest of the C API with `[link]`
declarations.

| Capability | C API (`GCRuntimeC.h`) | C4 |
|---|---|---|
| Managed alloc / free | `c4_alloc(size, align)`, `c4_alloc_noscan(size, align)`, `c4_free(p)` | `new` / `delete` |
| Raw memory | `c4_raw_alloc`, `c4_raw_free` | `raw_alloc(uint64)`, `raw_free(void*)` |
| Collect | `c4_gc_collect` | `gc_collect()`, `GC::Collect()` |
| Pacing | `c4_gc_set_percent(percent)` | `GC::SetPercent` |
| Marking threads | `c4_gc_set_mark_threads(threads)` | `GC::SetMarkThreads` |
| Disable / enable automatic collection | `c4_gc_disable()`, `c4_gc_enable()` | `GC::Disable`, `GC::Enable` |
| Scan regions | `c4_gc_register_region(start, size)`, `c4_gc_unregister_region(start)` | `gc_register_region`, `gc_unregister_region`, `GC::RegisterRegion`, `GC::UnregisterRegion` |
| Root enumerators | `c4_gc_register_root_enumerator(key, fn, user)`, `..._unregister_...(key)` | `GC::RegisterRootEnumerator`, `GC::UnregisterRootEnumerator` |
| Another image's root table (a script's globals, in the `__c4_gc_roots` form) | `c4_gc_add_root_table(table)`, `c4_gc_remove_root_table(table)` | used by the script host (docs/runtime/04) |
| Drop the finalizers in a code range (before that code unloads) | `c4_gc_drop_finalizers_in(low, high)`: the registered and the queued ones; returns how many | used by the script host |
| Post-collection callbacks | `c4_gc_register_collection_result(key, fn, user)`, `c4_gc_is_reachable(p)` | `GC::RegisterCollectionResult`, `GC::IsReachable` |
| Pin / unpin (counted) | `c4_gc_pin(p)`, `c4_gc_unpin(p)` | `GC::Pin`, `GC::Unpin` |
| Stats | `c4_gc_get_stats`, `c4_gc_allocs`, `c4_gc_collections`, `c4_gc_heap_bytes`, `c4_gc_free_bytes`, `c4_gc_region_count` | `GC::GetStats()`, `gc_allocs()`, `gc_collections()`, `gc_heap_bytes()`, `gc_free_bytes()` |
| Finalizer registration | `c4_gc_register_finalizer(obj, fn)` | implicit through `[operator("delete")]` |
| Stats printer | `c4_gc_set_stats_printer`, `c4_gc_install_default_stats_printer` | the `-gc-stats` flag: the program installs the default printer at startup, which writes a `[GC] allocs=... collections=... heapBytes=... liveBytes=... freeBytes=... pauseNs=... lastPauseNs=...` line to stderr after each collection |
| Byte helpers | `c4_byte_copy/move/at`, `c4_set_byte` (`C4Runtime/MemoryRuntimeC.c`) | `byte_copy`, `byte_move`, `byte_at`, `set_byte` |
| Thread registration | `c4_gc_register_thread()`, `c4_thread_attach()` | `gc_register_thread()`; `thread_attach()` also starts the thread's thread_local objects (`ThreadRuntimeC.h`); a thread `thread_start` started is attached already |
| Threads | `c4_thread_start(entry, state)`, `c4_thread_join`, `c4_thread_detach`, `c4_thread_current_id`, `c4_thread_hardware_count`, `c4_thread_yield`, `c4_thread_sleep_ns` (`C4Runtime/ThreadRuntimeC.c`) | `thread_start(entry, state)` (state stays alive until the new thread holds it), `thread_join`, `thread_detach`, `thread_current_id`, `thread_hardware_count`, `thread_yield`, `thread_sleep_ns` |
| Waiting on an address | `c4_wait_on_address(address, expected, size, timeoutNs)`, `c4_wake_one`, `c4_wake_all` | `wait_on_address`, `wake_one`, `wake_all`: block while the bytes at `address` equal `expected` (a negative timeout waits forever; it may return early, so check the condition again) |
| The operating system: virtual memory, system and process information, libraries, time | `c4_vm_*`, `c4_system_*`, `c4_library_*`, `c4_time_*` (`C4Runtime/PlatformRuntimeC.h`) | Bedrock's `Platform` namespace ([Bedrock section 12](../library/bedrock.md#12-platform-platform)); memory it hands out is never scanned |

Every function that takes an object pointer (`c4_free`, pin, unpin, `c4_gc_is_reachable`, the visit
callback of an enumerator) accepts any pointer into the object.

### Engine hooks

These exist so an engine can use custom allocators and handle-based storage without fighting the GC.

- **Scan regions**: conservative scanning of memory you own (native buffers, FFI state). Unregister before
  freeing. Registering the same start again replaces its size.
- **Root enumerators**: for storage that holds indices, offsets or handles instead of pointers. Once per
  collection your callback `fn(user, visit, visitContext)` turns handles into pointers and reports each by
  calling `visit(visitContext, ptr)`; the GC then traces them like any other root. It runs while every other
  thread is stopped, so it must not take a lock another thread may hold (the C runtime's heap included).
- **Collection results**: callbacks that run after the sweep of each collection. Inside them
  `c4_gc_is_reachable(ptr)` tells you whether an object survived, so a handle allocator can recycle slots
  and detect stale handles.
- **Pinning**: keeps an object, and what it refers to, alive while native code holds a raw pointer across a
  GC window. Pins are counted.
- **Disable windows**: `c4_gc_disable()` / `c4_gc_enable()` stop automatic collection during a
  latency-critical section; an explicit collection still runs inside the window.

Hooks may register or unregister hooks while they run. There is no limit on the number of regions, pins,
enumerators or callbacks.

---

## 4. Known gaps

| Gap | Status |
|---|---|
| Win32 only (`GCPlatformWin32.c`). Linux needs a `GCPlatformPosix.c` (B31). | **Planned** |
| A pause is proportional to the live heap: about 6 ms per 50 MiB with 8 markers (`Benchmarks/gc/`). A concurrent mark, with a write barrier in codegen, would bound it. | **Planned** |
| Conservative scanning can keep an object alive while a stale copy of its address sits in a register or a dead stack slot. Precise stack maps would remove that (an idea, see section 5). | By design |

---

## 5. Intended direction

From LongTermPlan.txt item 5 ("GC audit") and the user's statement that C4 uses Go-style collection:

1. **Extensive testing and performance testing** of the collector, including automatic cleanup under
   churn and frame-time measurements (B32). **Planned**
2. **Custom-allocator hooks**: an allocator must be able to split one allocation into many objects and
   have the GC track each object inside it directly, and decide itself whether objects in that
   allocator should be cleaned up at all. Root enumerators and collection results cover part of this;
   still missing is an API to describe sub-object boundaries inside a region (per-object tracking and
   finalization) and a "should this be collected" decision callback. **Planned**
3. **Threading** (D-48): suspend-and-scan of registered threads and per-thread allocation caches per size
   class. **Implemented**; a concurrent mark that shortens the pause is a later step.

Possible future ideas from the older "universal GC" design notes. These are not commitments; each
needs a decision before work starts:

- Compiler-driven frame arenas with promotion to the GC heap.
- Young/old generations (would need a write barrier in codegen's store helper).
- Concurrent marking or lazy sweeping, for shorter pauses.
- Precise stack scanning from LLVM stack maps, with conservative scanning as a fallback.
