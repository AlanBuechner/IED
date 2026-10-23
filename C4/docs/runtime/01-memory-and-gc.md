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

**Implemented** - conservative, non-moving mark-sweep (D-47) over every thread that runs C4 code (D-48): a heap
past 16 MiB live is marked mostly concurrently (D-79), a smaller one stop-the-world. Windows x64.

| Aspect | Behaviour |
|---|---|
| Heap | 64 MiB arenas from the OS (huge objects get a run of contiguous arenas). A two-level map from address to arena gives the span of any 8 KiB page in O(1). |
| Objects | No headers. Objects up to 32 KiB live in slots of 72 size classes (16-byte steps to 256, then 8 steps per doubling; at most 1/8 of a slot wasted); larger ones, and alignments above 64, get pages of their own. A span's bitmaps record which slots hold objects and which are marked. Alignment: any power of two up to 8192. |
| Pointer-free objects | The compiler allocates objects of pointer-free types (`TypeHandle::MayHoldPointers`) with `c4_alloc_typed_noscan`; they live in separate spans that marking never scans. |
| Roots | `__c4_gc_roots` (every global, static member and static local that may hold a pointer, as address ranges); every registered thread's registers and stack, and its copy of each thread_local that may hold a pointer (`__c4_gc_thread_roots`); registered scan regions; root enumerators; pinned objects; objects waiting for their finalizer. |
| Threads | A thread registers on its first allocation, through `c4_gc_register_thread()`, or when it attaches (`thread_start` attaches a thread before its entry runs, `thread_attach()` attaches any other); the main thread registers before global initialization. As a thread ends, its thread_local objects are destroyed before it unregisters. A collection holds the collector's lock, suspends every other registered thread (`SuspendThread`, then `GetThreadContext` so it has really stopped), scans each one's full register context and its stack from its stack pointer up, and resumes them after the sweep. A thread unregisters when it ends. A thread that never allocated and never registered is not scanned: it must not hold the only reference to an object. |
| Thread caches | Each thread allocates small objects from spans its cache owns, without the lock: claiming a slot is an atomic bit set, so a `delete` on another thread can free slots of an owned span meanwhile. A collection waits out a thread inside that fast path before scanning it, and hands every cached span back to the heap before the sweep. The lock is a re-entrant critical section; the collector's own memory comes from a private heap taken only under it, so no stopped thread can hold a lock the collection needs. |
| Mark | An explicit mark stack (no recursion). Every aligned word of a scanned range that points anywhere into a live object marks it. A heap past 8 MiB is marked in parallel: helper threads (started on first use, parked between collections, never registered or stopped) drain beside the collecting thread, each from its own stack, sharing work through a pool that busy markers fill while others are idle; a mark bit is claimed atomically, so each object is scanned once. Root ranges (globals, scan regions, other threads' stacks and thread_locals) are queued in 16 KiB chunks for every marker; the collecting thread's own stack and the stopped threads' registers are scanned on the spot. Root enumerators run first, alone. `c4_gc_set_mark_threads` sets the markers (default: the processors, up to 8; 1 marks alone). |
| Finalize | After marking: unreachable finalizable objects are kept with what they reach and queued (see section 1); their finalizers run after the collection, without the lock and one thread at a time, through `c4_gc_run_finalizer`. Collection-result callbacks also run without the lock, so both may allocate, block or start threads. Finalizers, root enumerators, collection-result callbacks and a pool's `keep` and `released` all run through `GC/GCCallbacks.cpp`: an exception escaping one is a fatal error, never an unwind through the collector, so no collector entry point unwinds into its caller. |
| Sweep | The marked slots become the allocated slots; spans left empty return their pages. Free pages coalesce; runs of at least 512 KiB that stayed free for a whole collection are returned to the OS and recommitted on reuse. |
| Trigger | Automatic, checked when an allocation leaves its thread's cache: a collection starts when the live bytes reach `live after the last collection x (1 + percent/100)`, at least 4 MiB, or when the native memory in use (`c4_gc_stats.nativeBytes`: strings' and containers' buffers, which their objects free when finalized) reaches `what was in use once the last collection's finalizers had run x (1 + percent/100)`, and at least 32 MiB more (`GC_MIN_NATIVE_GROWTH`), so garbage that is small in the heap but holds big buffers is collected too (BUG-383). `percent` is 100 by default (`c4_gc_set_percent`). `c4_gc_disable` suppresses it; `gc_collect()` always works. Out of memory is a fatal error after one forced collection. A fatal error of the collector is reported as any is (`c4_fatal`: the message, the thread and its stack, on stderr and in the crash log; [library/bedrock.md](../library/bedrock.md) "Crash reports"). |
| Concurrent marking | D-79, a mostly-concurrent mark-sweep (Boehm, Demers and Shenker). An automatic collection of a heap of at least `c4_gc_set_concurrent` bytes (default 16 MiB; `C4_GC_CONCURRENT=always/never/<bytes>`) is a cycle run by a thread of the collector's own: it forgets which heap pages were written (the arenas are mapped with `MEM_WRITE_WATCH`), stops the program to copy its roots (stacks, registers, globals, thread_locals, regions, enumerated handles, pins, keep-all pools, the finalizer queue) and marks from the copies while the program runs, with the parallel helpers; up to three times it copies the roots again and rescans the marked objects on the pages written meanwhile; a final stop marks from the roots and the pages written since, then selects finalizers, decides pools and sweeps. Objects allocated during a cycle start unmarked: the ones still reached are found at the final stop, the rest go in this cycle. The stops are each under about 1 ms on the GC benchmark's 52 MiB world (p99 frame 1.1 ms, stop-the-world 5.2 ms). A program whose allocation outruns a cycle (the live heap doubles) finishes it at once; `c4_gc_collect` finishes a running cycle and then stops the world. The cycle thread runs collection callbacks, pool releases and finalizers after each cycle. `c4_gc_set_verify` / `C4_GC_VERIFY=1` checks every cycle against a fresh mark from the roots (a missed object is a fatal error naming it); `C4_GC_TRACE=1` prints a line per cycle. |
| Write barrier | None: the OS tracks written pages, so compiled code, native code and memcpy store pointers freely. |
| Stats | `c4_gc_get_stats()` returns `{ allocs, collections, heapBytes, freeBytes, pauseNs, liveBytes, lastPauseNs, nativeBytes }` (`nativeBytes`: native memory in use, below). `heapBytes` is the memory held from the OS, `freeBytes` the part of it not holding objects. A thread sees its own allocations in the counts at once; another thread's arrive when it next refills its cache or a collection runs. |
| Allocation counts | Every allocation also adds one object and the bytes asked for to its thread's own counters (`GCThread.allocated`), written only by that thread and never reset; a pool slot counts once when `c4_gc_pool_occupy` takes it vacant, at the slot's size. `c4_gc_thread_allocated()` reads the calling thread's without the lock (a profiler takes it around a scope); `c4_gc_allocated()` sums every thread's under the lock, with what threads that ended allocated (folded in at thread exit). Exact at any moment, unlike `allocs`. Native memory is counted beside it: `c4_raw_alloc` (at the size asked, given back at `_msize` or at the size `c4_raw_free_sized` is told; an aligned block keeps its size in front of it) and the container buffers (`c4_buffer_resize` adds what a buffer grows by and gives back what it shrinks by, `c4_buffer_free` gives back the rest) report through `c4_gc_native_taken` / `_given`. A registered thread adds what it takes to its `allocated.nativeBytes`, what it gives back to its `allocated.nativeFreed` (whichever thread took it), and keeps its in-use balance on its own record (plain adds, no atomic); other threads' go to one atomic total. `c4_gc_stats.nativeBytes` sums the balances under the lock. |
| Census | Off by default. While `c4_gc_set_census(true)` holds, every allocation notes its type's record (`c4_alloc_typed`'s `c4_gc_type { name, nativeBytes }`, one the compiler writes per type `new` makes, a lambda's closure named for the code it is in, `lambda in Profile::StartSession` or `lambda #2 in ...`; none from `c4_alloc` or a pool) in a table beside its span's slots (`GC/GCCensus.c`; 8 bytes a slot, made the first time the span is allocated from and freed with it), written by the thread that owns the span; turning it on starts a new epoch, so tables from an earlier census read as unknown. At the end of each collection (`gc_reclaim`, every thread stopped) the survivors are counted by the records noted, one entry a name (a program and each script have their own record of a type, merged by name), for `c4_gc_last_census`. A record's `nativeBytes` (null for a type that holds no native memory) is the compiler's `native_bytes_of` of one object ([language/14](../language/14-builtins.md#1-compiler-intrinsics)), or for an array `new T[n]`, of its elements: the census calls it on each survivor of the type, under a guard that counts a faulting read as 0 (another thread may be stopped between freeing a buffer and storing its replacement), and the entry's `nativeBytes` is their sum. Objects made before it was on are of no known type, and so are a script's once it is unloaded (`c4_gc_forget_types_in` its image, beside `c4_gc_drop_finalizers_in`), since its records go with it. |
| Sampled allocations | Off by default. `c4_gc_set_alloc_sampling(everyBytes)` (`GC/GCSampling.c`) records about one allocation in every `everyBytes` a thread allocates: each thread counts its bytes down to a point drawn from the exponential distribution of that mean (so no allocation pattern lines up with the points, and each sample stands for `everyBytes` bytes on average), and the allocation that crosses it is recorded with its size, time, thread and up to 32 return addresses from the allocating function out (walked by each function's unwind data, `RtlVirtualUnwind`, within the thread's stack and outside the lock: `RtlCaptureStackBackTrace` finds no frame when called deeper than the stack has been before, BUG-342). The fast path only subtracts and compares; a thread looks at the setting again at least every 64 KiB, so another thread takes a change up within that, the calling thread at once. Records wait in a ring of the latest 4096 until `c4_gc_take_alloc_samples` moves them out. |

Source layout (`C4Runtime/GC/`): `GCPlatformWin32.c` (OS memory, the lock and private heap, thread suspension
and stack scans, thread exit, clock - the only platform file), `GCThreads` (the thread registry, per-thread
caches and their fast path), `GCPageHeap` (arenas, page map, free runs), `GCHeap` (size-class spans, allocation, release, sweep),
`GCMark` (mark stack, range scanning, root snapshots, verification), `GCConcurrent` (concurrent cycles), `GCFinalize`
(finalizer selection and queue), `GCPools` (pools: their spans,
keep decisions, the release queue), `GCRoots` (root
marking, pins), `GCPtrMap` (the hash map behind finalizers, pins, regions and hooks), `GCCollector` (the
collection cycle, pacing), `GCApi.c` (the exported functions; the only file with process state).

---

## 3. What C4 code can reach

The builtins are declared in the prelude (`Bedrock/Prelude/prelude.ch`) and need no include. The Bedrock `GC` namespace (`Bedrock/Memory/gc.ch`) covers the rest of the C API with `[link]`
declarations.

| Capability | C API (`GCRuntimeC.h`) | C4 |
|---|---|---|
| Managed alloc / free | `c4_alloc(size, align)`, `c4_alloc_noscan(size, align)`, `c4_free(p)`; `c4_alloc_typed(size, align, type)` and `c4_alloc_typed_noscan` also give the object's `c4_gc_type` record for a census | `new` (typed) / `delete` |
| Raw memory | `c4_raw_alloc`, `c4_raw_free`, `c4_raw_free_sized`, `c4_raw_alloc_aligned`, `c4_raw_free_aligned` | `raw_alloc(uint64)`, `raw_free(void*)`, `raw_free_sized(void*, uint64)`, `raw_alloc_aligned`, `raw_free_aligned` |
| Buffers that grow in place past 1 MiB (`C4Runtime/BufferRuntimeC.c`) | `c4_buffer_resize(buffer, oldBytes, keepBytes, newBytes)`, `c4_buffer_free(buffer, bytes)`, and `_scanned` forms that keep a scan region over the buffer | `buffer_resize`, `buffer_free`, `buffer_resize_scanned`, `buffer_free_scanned` ([builtins 4](../language/14-builtins.md#4-memory-and-gc)) |
| Collect | `c4_gc_collect` | `gc_collect()`, `GC::Collect()` |
| Pacing | `c4_gc_set_percent(percent)` | `GC::SetPercent` |
| Marking threads | `c4_gc_set_mark_threads(threads)` | `GC::SetMarkThreads` |
| Disable / enable automatic collection | `c4_gc_disable()`, `c4_gc_enable()` | `GC::Disable`, `GC::Enable` |
| Concurrent marking | `c4_gc_set_concurrent(bytes)`, `c4_gc_set_verify(on)` | `GC::SetConcurrent`, `GC::SetVerify` |
| Scan regions | `c4_gc_register_region(start, size)`, `c4_gc_unregister_region(start)` | `gc_register_region`, `gc_unregister_region`, `GC::RegisterRegion`, `GC::UnregisterRegion` |
| Root enumerators | `c4_gc_register_root_enumerator(key, fn, user)`, `..._unregister_...(key)` | `GC::RegisterRootEnumerator`, `GC::UnregisterRootEnumerator` |
| Another image's root table (a script's globals, in the `__c4_gc_roots` form) | `c4_gc_add_root_table(table)`, `c4_gc_remove_root_table(table)` | used by the script host (docs/runtime/04) |
| Drop the finalizers in a code range (before that code unloads) | `c4_gc_drop_finalizers_in(low, high)`: the registered and the queued ones; returns how many | used by the script host |
| Post-collection callbacks | `c4_gc_register_collection_result(key, fn, user)`, `c4_gc_is_reachable(p)` | `GC::RegisterCollectionResult`, `GC::IsReachable` |
| Pools: a custom allocator's objects tracked one by one | `c4_gc_pool_create(desc)` (`slot_size`, `slot_count`, `noscan`, `keep_all`, `keep`, `release`, `user_data`), `c4_gc_pool_destroy`, `c4_gc_pool_base` / `_slot_size` / `_slot_count`, `c4_gc_pool_occupy` / `_vacate` / `_occupied` | `GC::Pool` ([Bedrock 7](../library/bedrock.md#pools-memorypoolch)) |
| Pin / unpin (counted) | `c4_gc_pin(p)`, `c4_gc_unpin(p)` | `GC::Pin`, `GC::Unpin` |
| Stats | `c4_gc_get_stats`, `c4_gc_allocs`, `c4_gc_collections`, `c4_gc_heap_bytes`, `c4_gc_free_bytes`, `c4_gc_region_count` | `GC::GetStats()`, `gc_allocs()`, `gc_collections()`, `gc_heap_bytes()`, `gc_free_bytes()` |
| Census of live objects by type (`{ type, objects, bytes, nativeBytes }`, one entry a type name, by name, the unknown (`NULL`) first) | `c4_gc_set_census(on)` (returns whether it was on), `c4_gc_last_census(out, capacity)`: how many entries the last census has | `GC::SetCensus`, `GC::LastCensus` with `GC::CensusEntry` |
| Allocations counted (`{ objects, bytes, nativeBytes, nativeFreed }`) | `c4_gc_thread_allocated()`: the calling thread's since it registered, no lock; `c4_gc_allocated()`: every thread's, ended ones included | `GC::ThreadAllocated()`, `GC::Allocated()` as `GC::AllocCounts` |
| Sampled allocations (`{ bytes, timeNs, threadId, frameCount, frames[32] }`) | `c4_gc_set_alloc_sampling(everyBytes)` (0: off; returns the previous setting), `c4_gc_take_alloc_samples(out, capacity)`: how many it moved, oldest first | `GC::SetAllocSampling`, `GC::TakeAllocSamples` with `GC::AllocSample` |
| The last collection's stops (`{ startNs, endNs }` each, oldest first, on the QueryPerformanceCounter clock in nanoseconds: one for a stop-the-world collection, one per root copy and one to finish for a concurrent cycle) | `c4_gc_last_pauses(out, capacity)`: how many there were | `GC::LastPauses(Pause* out, int32 capacity)`, read in a collection callback for exact GC lanes in a profiler |
| Finalizer registration | `c4_gc_register_finalizer(obj, fn)` | implicit through `[operator("delete")]` |
| Stats printer | `c4_gc_set_stats_printer`, `c4_gc_install_default_stats_printer` | the `-gc-stats` flag: the program installs the default printer at startup, which writes a `[GC] allocs=... collections=... heapBytes=... liveBytes=... freeBytes=... pauseNs=... lastPauseNs=...` line to stderr after each collection |
| Byte helpers | `c4_byte_copy/move/at`, `c4_set_byte` (`C4Runtime/MemoryRuntimeC.c`) | `byte_copy`, `byte_move`, `byte_at`, `set_byte` |
| Thread registration | `c4_gc_register_thread()`, `c4_thread_attach()` | `gc_register_thread()`; `thread_attach()` also starts the thread's thread_local objects (`ThreadRuntimeC.h`); a thread `thread_start` started is attached already |
| Threads | `c4_thread_start(entry, state)`, `c4_thread_join`, `c4_thread_detach`, `c4_thread_current_id`, `c4_thread_hardware_count`, `c4_thread_yield`, `c4_thread_sleep_ns`, `c4_thread_set_name(name)` / `c4_thread_name(out, cap)` (the calling thread's name as debuggers, profilers and crash dumps show it, UTF-8) (`C4Runtime/ThreadRuntimeC.c`) | `thread_start(entry, state)` (state stays alive until the new thread holds it), `thread_join`, `thread_detach`, `thread_current_id`, `thread_hardware_count`, `thread_yield`, `thread_sleep_ns`, `thread_set_name`, `thread_name`; `Threading::Thread::SetCurrentName` / `CurrentName` |
| Waiting on an address | `c4_wait_on_address(address, expected, size, timeoutNs)`, `c4_wake_one`, `c4_wake_all` | `wait_on_address`, `wake_one`, `wake_all`: block while the bytes at `address` equal `expected` (a negative timeout waits forever; it may return early, so check the condition again) |
| The operating system: virtual memory, system and process information, random bytes, libraries, time | `c4_vm_*`, `c4_system_*`, `c4_library_*`, `c4_time_*` (`C4Runtime/PlatformRuntimeC.h`) | Bedrock's `Platform` namespace ([Bedrock section 12](../library/bedrock.md#12-platform-platform)); memory it hands out is never scanned |
| Sockets: TCP, UDP, name resolution, polling | `c4_net_*` (`C4Runtime/NetRuntimeC.h`) | Bedrock's `Net` namespace ([Bedrock section 13](../library/bedrock.md#13-networking-net)) |
| Local connections between one user's processes (named pipes) | `c4_local_*` (`C4Runtime/LocalRuntimeC.h`) | Bedrock's `Net::LocalListener`, `Net::LocalStream` ([Bedrock section 13](../library/bedrock.md#13-networking-net)) |
| Child processes: pipes, job objects, pseudo consoles | `c4_process_*` (`C4Runtime/ProcessRuntimeC.h`) | Bedrock's `Process` namespace ([Bedrock section 15](../library/bedrock.md#15-processes-process)) |
| File information, copies, real paths, watching a directory | `c4_fs_info`, `c4_fs_copy_file`, `c4_fs_canonical`, `c4_fs_watch_*`, ... (`C4Runtime/FileSystemRuntimeC.h`) | Bedrock's `FS` namespace ([Bedrock section 5](../library/bedrock.md#5-files-paths-and-directories-fs)) |

Every function that takes an object pointer (`c4_free`, pin, unpin, `c4_gc_is_reachable`, the visit
callback of an enumerator) accepts any pointer into the object.

### Engine hooks

These exist so an engine can use custom allocators and handle-based storage without fighting the GC.

- **Scan regions**: conservative scanning of memory you own (native buffers, FFI state). Unregister before
  freeing. Registering the same start again replaces its size.
- **Root enumerators**: for storage that holds indices, offsets or handles instead of pointers. Once per
  stop-the-world collection (in each stop of a concurrent cycle) your callback `fn(user, visit, visitContext)` turns handles into pointers and reports each by
  calling `visit(visitContext, ptr)`; the GC then traces them like any other root. It runs while every other
  thread is stopped, so it must not take a lock another thread may hold (the C runtime's heap included).
- **Collection results**: callbacks that run after the sweep of each collection. Inside them
  `c4_gc_is_reachable(ptr)` tells you whether an object survived, so a handle allocator can recycle slots
  and detect stale handles.
- **Pinning**: keeps an object, and what it refers to, alive while native code holds a raw pointer across a
  GC window. Pins are counted.
- **Disable windows**: `c4_gc_disable()` / `c4_gc_enable()` stop automatic collection during a
  latency-critical section; an explicit collection still runs inside the window. `c4_gc_disable` first finishes a
  concurrent cycle under way, so no automatic collection completes inside the window.
- **Pools** (D-78): a block a custom allocator divides into objects of one size, which the collector tracks one by
  one. The allocator occupies and vacates slots; the pool's pages are spans of 512 slots each (`GC_SPAN_POOL`), so
  finding and marking a pool object is finding and marking any object, and a pointer into a slot keeps that object
  only. A collection marks the occupied objects of a `keep_all` pool as roots; after the drain it offers each occupied
  object nothing reached to the pool's `keep` callback (a kept one is marked and drained at once, so what it reaches
  is never offered); the sweep vacates the rest and queues them, and after the collection one thread at a time hands
  each to the pool's `release` callback without the lock. Destroying a pool drops its queued releases. Occupied
  bytes count as live bytes for pacing; `delete` and finalizers do not apply to pool objects.

Hooks may register or unregister hooks while they run. There is no limit on the number of regions, pins,
enumerators or callbacks.

---

## 4. Known gaps

| Gap | Status |
|---|---|
| Win32 only (`GCPlatformWin32.c`). Linux needs a `GCPlatformPosix.c` (B31). | **Planned** |
| Conservative scanning can keep an object alive while a stale copy of its address sits in a register or a dead stack slot. Precise stack maps would remove that (an idea, see section 5). | By design |

---

## 5. Intended direction

From LongTermPlan.txt item 5 ("GC audit") and the user's statement that C4 uses Go-style collection:

1. **Extensive testing and performance testing** of the collector, including automatic cleanup under
   churn and frame-time measurements (B32). **Planned**
2. **Custom-allocator hooks**: an allocator must be able to split one allocation into many objects and
   have the GC track each object inside it directly, and decide itself whether objects in that
   allocator should be cleaned up at all. **Implemented** (B63, D-78): pools, with per-object tracking, a
   release callback in place of finalization, `keep_all` and a per-object `keep` decision.
3. **Threading** (D-48): suspend-and-scan of registered threads and per-thread allocation caches per size
   class. **Implemented**; concurrent marking (D-79, B64) keeps the pauses short whatever the live heap.

Possible future ideas from the older "universal GC" design notes. These are not commitments; each
needs a decision before work starts:

- Compiler-driven frame arenas with promotion to the GC heap.
- Young/old generations (would need a write barrier in codegen's store helper).
- Lazy or concurrent sweeping, for a shorter final stop on a very large heap.
- Precise stack scanning from LLVM stack maps, with conservative scanning as a fallback.
