---
name: c4-garbage-collector
description: C4 garbage collector - non-moving mark-sweep, precise for heap objects by compiler pointer maps and conservative for stacks and raw regions, mostly concurrent past 16 MiB live (OS write watch, no barrier), with size-class spans, an O(1) page map, an explicit mark stack, ordered finalizers and noscan allocation for pointer-free types (C4Runtime/GC/, linked into every executable). Covers the managed-pointer model, roots, raw allocation, scan regions, root enumerators, collection callbacks, custom-allocator pools, counted pins, pacing, disable/enable windows, stats and the Bedrock GC namespace.
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

C4 uses a **non-moving mark-sweep** collector with **no write barriers** (it rescans everything each collection;
past 16 MiB live it marks mostly concurrently): **precise for heap objects of known type** (R16: compiler pointer
maps), **conservative for stacks, registers and raw regions**. The C API is `C4Runtime/GCRuntimeC.h`; the implementation is
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
- Heap objects are scanned precisely (R16): every `c4_gc_type` record `new` passes carries the type's pointer map
  (`CodeGen/PointerMaps.cpp`: which words may hold a pointer - pointer/reference fields, a `func`'s words, a union's
  word when a member may be one; not vptrs, function pointers or numbers); `new` of such a type calls
  `c4_alloc_precise` (`new T[n]`: `c4_alloc_precise_array`), and marking reads only those words of an object.
  Precise spans hold one type each and keep its interned map (`GCSpan.map`, `GC/GCPointers.{h,c}`); a thread finds
  its span for a type by the record pointer (`GCThreadCache.typed`), so a precise `new` writes and reads nothing an
  unmapped one does not. A type whose every word may hold a pointer has no map and is scanned whole
  (`c4_alloc_typed`). `new T[n]` repeats the element's map after a counted array's count. Keep the mark and
  allocation fast paths inline (`push_contents`, `gc_thread_alloc_typed`), the record off the fast path, and the hot
  `GCSpan` fields before the bitmaps: each of those cost the GC benchmark 1-25% when lost.
  Buffers too: `gc_type_of(T)` (a builtin) names T's record, and a region registered with it
  (`gc_register_typed_region`, `buffer_resize_scanned_typed`; a reflected type's `Reflection::Type::GcType()`) is
  read by T's map; Bedrock's containers and serializers register theirs so.
  Still conservative: stacks, registers, untyped scan regions, `c4_alloc` memory - stacks unless the program is built
  with `-fstack-maps` (R16 phase 3, off by default, at no measurable run-time cost). With it each C4 frame waiting in a
  call is read only where LLVM's stack map says its live values and pointer-holding slots are (`Compiler/StackMap*`: after
  the optimizer each call gets a `deopt` bundle naming them, nothing relocated, the IR otherwise unchanged - keep it so:
  a statepoint operand counts as a use to the register allocator, so a value a loop does not use is named through a slot
  of its own), every other frame (C, runtime, scripts, the innermost one, calls left without a record) whole with the
  callee-saved registers it saved; an explicit collection does not read its own frames. The walk unwinds without loader
  locks (`GC/GCStackWalk{Win32,Posix}.c`) and the program links the runtime built with `C4_STACK_MAPS` (`GC/GCStack*`
  only there). Build and test it with `RunTests.ps1 -StackMaps` (`run_linux.py --stack-maps`).

## Roots
`__c4_gc_roots` (compiler-emitted `{address, size, pointers}` ranges for every global, static member and static
local that may hold a pointer; `pointers` the storage type's map, R16 phase 2, so only those words are read - null
reads the range whole; thread_locals the same through `__c4_gc_thread_roots`), every registered thread's registers
and stack (conservative; with `-fstack-maps` C4 frames by their stack maps), registered scan regions,
root enumerators, pinned objects and objects waiting for their finalizer. A conservative collector can keep
an object alive while a stale copy of its address sits in a register or dead stack slot - tests that assert
"was collected" must allow a few survivors, or collect explicitly below many C4 frames with `-fstack-maps` (a runtime
or C frame other than the collection's own is read whole: give it stack memory no earlier call used, as
`MemoryGC/StackMaps` does).

## Everyday builtins (no include)
`raw_alloc(size)` / `raw_free(ptr)`; `gc_collect()`; `gc_register_region(start, size)` /
`gc_unregister_region(start)`; `gc_allocs()`, `gc_collections()`, `gc_heap_bytes()`, `gc_free_bytes()`.
Build with `-gc-stats` and the program prints `[GC] allocs=... collections=... heapBytes=...` to stderr after each collection.

## Bedrock `GC` namespace (`include "Memory/gc.ch"`)
`GC::Collect`, `GC::GetStats` (`{ allocs, collections, heapBytes, freeBytes, pauseNs, liveBytes,
lastPauseNs }`), `GC::SetPercent` (growth before the next automatic collection, default 100, minimum
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
  Pacing counts from a cycle's end: what the program allocates while a cycle marks is not counted (new objects stay
  unmarked, so the garbage among them goes in that cycle), so a cycle that marks faster leaves the next one less to
  wait for - precise marking runs ~half a cycle more in the GC benchmark's 600 frames, each one shorter, frames even.
  Read a benchmark's cycle count as a mean over many runs: where the last trigger falls against the run's end moves it.
- **Diagnosing a freed object still in use** (heap corruption, a crash in malloc or in a finalizer): run with
  `C4_GC_VERIFY_MAPS=1` (or `=<file>`) - each word a pointer map says holds no pointer but that names a heap object is
  reported (`[gc] map misses a pointer: word W of an object of T`, `UNMARKED` when the sweep frees its target) - and
  `C4_GC_STRESS=N` (a collection every Nth cache refill) with `C4_GC_POISON=1`, so a reference the collector does not
  see faults at its use. On Linux add glibc's checks: `LD_PRELOAD=libc_malloc_debug.so.0 GLIBC_TUNABLES=glibc.malloc.check=3
  MALLOC_PERTURB_=165`.
- **Exit**: once the program's exit begins (the collector's exit handler runs after every other one) a running cycle
  finishes and no collection stops a thread again, `Collect()` included (BUG-491: a stop the exit killed midway left
  the exiting thread suspended - a hang at exit, one run in five with threads collecting in a loop).
- **Use after free**: `GC::SetPoison(true)` / `C4_GC_POISON=1` fills every object a sweep or `delete` ends with `0xDB`
  bytes, so a pointer read from a freed object faults at its use. A crash in a finalizer (`__c4_delete_...` under
  `c4_gc_run_finalizer`) is usually the program's own: an object finalized after something it reaches was `delete`d
  (a GPU resource outliving its renderer), not the collector freeing a live object.
- **Pools** (`GC::Pool`, `include "Memory/pool.ch"`; D-78): a custom allocator's block of same-size objects that the
  collector tracks one by one. `Take()` a zeroed slot, `Give(object)` it back; a collection ends taken objects
  nothing reaches and frees their slots (`options.released` hears of each; `Take` collects once before saying the
  pool is full). `options.keepAll` keeps every taken object (and what it reaches) until given back; `options.keep`
  may keep one a collection would end (it runs with other threads stopped: no locks, no Take); `options.noscan` for
  pointer-free objects. `At(i)`, `IndexOf`, `Holds`; `delete` and finalizers do not apply to pool objects. The C API
  `c4_gc_pool_set_type(pool, type)` (before a slot is occupied) makes a pool's objects precise by a `c4_gc_type`.

## Internals (C4Runtime/GC/)
`GCPlatformWin32.c` (OS memory, register capture + stack scan, clock), `GCPageHeap` (64 MiB arenas,
two-level page map, coalescing free runs, scavenging to the OS), `GCHeap` (72 size classes, bitmaps, large
objects, sweep), `GCPointers` (precise spans' pointer bits), `GCMark` (explicit mark stack), `GCFinalize`, `GCRoots`,
`GCPtrMap`, `GCCollector`
(cycle, pacing), `GCPools` (pool spans, keep decisions, release queue), `GCApi.c` (exports; the only process
state). Threads are D-48: suspend-and-scan with per-thread caches and parallel marking.
