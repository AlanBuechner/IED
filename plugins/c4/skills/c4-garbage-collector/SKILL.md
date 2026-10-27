---
name: c4-garbage-collector
description: C4 garbage collector - conservative, non-moving mark-sweep, mostly concurrent past 16 MiB live (OS write watch, no barrier), with size-class spans, an O(1) page map, an explicit mark stack, ordered finalizers and noscan allocation for pointer-free types (C4Runtime/GC/, linked into every executable). Covers the managed-pointer model, roots, raw allocation, scan regions, root enumerators, collection callbacks, custom-allocator pools, counted pins, pacing, disable/enable windows, stats and the Bedrock GC namespace.
triggers:
  - "C4 garbage collector"
  - "non-moving mark-sweep stop-the-world finalizer"
  - "raw_alloc unmanaged memory"
  - "register scan region root enumerator"
  - "pin object gc_disable window"
  - "gc stats pauseNs collections"
  - "GC namespace Memory/gc.ch"
---

# C4 garbage collector

C4 uses a **conservative, non-moving, stop-the-world mark-sweep** collector with **no write barriers** (it
rescans everything each collection). The C API is `C4Runtime/GCRuntimeC.h`; the implementation is
`C4Runtime/GC/` (pure C plus a C++ finalizer shim), linked into every executable. The reference is
`docs/runtime/01-memory-and-gc.md`.

## Model
- Every pointer (`T*`) is a managed GC pointer; there is no raw pointer type at the language level.
- Value-type locals are RAII. A heap object's `[operator("delete")]` is its finalizer: it runs once, on
  `delete p` (any pointer into the object; `delete null` is a no-op) or after the collection that finds the
  object unreachable.
- Finalizers run after the collection, never inside it. An unreachable finalizable object keeps what it
  refers to alive for its finalizer; if one unreachable finalizable object refers to another, the referrer
  is finalized first. Self-pointers do not delay an object; cycles are finalized one object per collection.
  A finalizer may allocate. An exception escaping a finalizer is fatal.
- Objects of pointer-free types are allocated `noscan`: a pointer kept in such an object as an integer does
  not keep anything alive. Hiding pointers in integers is not supported.

## Roots
`__c4_gc_roots` (compiler-emitted `{address, size}` ranges for every global, static member and static local
that may hold a pointer), the calling thread's registers and stack (conservative), registered scan regions,
root enumerators, pinned objects and objects waiting for their finalizer. A conservative collector can keep
an object alive while a stale copy of its address sits in a register or dead stack slot - tests that assert
"was collected" must allow a few survivors.

## Everyday builtins (no include)
`raw_alloc(size)` / `raw_free(ptr)`; `buffer_resize` / `buffer_free` (a buffer that grows in place past 1 MiB;
`_scanned` forms keep a scan region over it, as Array's elements); `gc_collect()`; `gc_register_region(start, size)` /
`gc_unregister_region(start)`; `gc_allocs()`, `gc_collections()`, `gc_heap_bytes()`, `gc_free_bytes()`.
Build with `-gc-stats` and the program prints `[GC] allocs=... collections=... heapBytes=...` to stderr after each collection.

## Bedrock `GC` namespace (`include "Memory/gc.ch"`)
`GC::Collect`, `GC::GetStats` (`{ allocs, collections, heapBytes, freeBytes, pauseNs, liveBytes,
lastPauseNs }`), `GC::LastPauses(out, capacity)` (the last collection's stops as `{ startNs, endNs }` on the
QueryPerformanceCounter clock: one for `Collect`, several for a concurrent cycle), `GC::SetPercent` (growth before the next automatic collection, default 100, minimum
trigger 4 MiB), `GC::Disable` / `GC::Enable`, `GC::Pin` / `GC::Unpin` (counted, interior pointers fine),
`GC::IsReachable`, `GC::RegisterRegion`, `GC::RegisterRootEnumerator(key, fn(user, visit, visitContext),
user)`, `GC::RegisterCollectionResult(key, fn(user), user)` and their unregister forms. No hook table has a
size limit, and hooks may (un)register hooks while they run.

## Engine hooks
- **Scan regions**: raw buffers holding managed pointers (Bedrock containers register their element buffers).
- **Root enumerators**: handle-based storage reports live objects each collection with `visit(visitContext, ptr)`.
- **Collection results**: after each sweep; `IsReachable` tells which objects survived.
- **Pins**: keep an object and its referents alive while native code holds it.
- **Disable windows**: suppress automatic collection in latency-critical sections; `Collect()` still works.
- **Concurrent marking** (D-79): a heap past `GC::SetConcurrent(bytes)` live (default 16 MiB) is collected by a cycle
  on the collector's own thread: a stop to copy the roots, marking beside the program, rescans of the pages it
  wrote (Windows write watch, so no write barrier), a final stop to finish. Stops stay under ~1 ms; `Collect()`
  stops the world after finishing a running cycle; finalizers of a cycle run on the collector's thread.
  `C4_GC_CONCURRENT=always|never|<bytes>`, `C4_GC_VERIFY=1` (check every cycle), `C4_GC_TRACE=1` (a line a cycle).
- **Pools** (`GC::Pool`, `include "Memory/pool.ch"`; D-78): a custom allocator's block of same-size objects that the
  collector tracks one by one. `Take()` a zeroed slot, `Give(object)` it back; a collection ends taken objects
  nothing reaches and frees their slots (`options.released` hears of each; `Take` collects once before saying the
  pool is full). `options.keepAll` keeps every taken object (and what it reaches) until given back; `options.keep`
  may keep one a collection would end (it runs with other threads stopped: no locks, no Take); `options.noscan` for
  pointer-free objects. `At(i)`, `IndexOf`, `Holds`; `delete` and finalizers do not apply to pool objects.

## Internals (C4Runtime/GC/)
`GCPlatformWin32.c` (OS memory, register capture + stack scan, clock), `GCPageHeap` (64 MiB arenas,
two-level page map, coalescing free runs, scavenging to the OS), `GCHeap` (72 size classes, bitmaps, large
objects, sweep), `GCMark` (explicit mark stack), `GCFinalize`, `GCRoots`, `GCPtrMap`, `GCCollector`
(cycle, pacing), `GCPools` (pool spans, keep decisions, release queue), `GCApi.c` (exports; the only process
state). Threads are D-48: suspend-and-scan with per-thread caches and parallel marking.
