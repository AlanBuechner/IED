# 16 - Profiler design and trace protocol

## Purpose

Resolve `docs/99` #9: define the seam between instrumented programs (C4 apps through Bedrock `Profile::`, game engines, anything else) and the IED profiler - the **IED Trace Protocol (ITP) v1** - and the profiler's server/client architecture. Feature list: `docs/features/12-profiler.md`.

Tracy is a **feature reference only** for IED; it is linked nowhere (the compiler's C++ libraries profile through the C/C++ ITP client, `plugins/cpp/libraries/Profiling/src/Profiling/Itp.h`). ITP deliberately fixes the Tracy limitations that matter for this project:

| Tracy limitation | ITP / IED answer |
|---|---|
| Thread name read once; no rename history | `ThreadName` is a timestamped event; names are a time series |
| No "by task" view; fibers create one permanent track per pointer | Explicit task events (`TaskBegin/End/Suspend/Resume`) with interned task *names* (not pointers) + fallback grouping by thread-name segments |
| Strings identified by client pointers (lifetime/aliasing rules, server reads client memory) | Explicit interning: `StringDef { id, utf8 }` sent before first use |
| Hard caps (65534 source locations, 2^47 ns sessions, 64 threads per lock...) | Varint ids and 64-bit timestamps; no fixed tables |
| Whole trace in RAM until save | Streamed to a chunked file as it arrives |
| Instrumentation error kills the connection | Errors become `Warning` events; capture continues |
| Annotations in a sidecar file | Annotations stored in the trace |

## Architecture

```
instrumented app                       ied-server (src/profiler)                      browser (plugins/profiler UI)
+-------------------+   TCP (ITP)   +-----------------------------------+   WS + blobs  +---------------------------+
| Bedrock Profile:: | ------------> | capture ingest -> chunk writer     | ------------> | timeline canvas (WebGL)    |
| per-thread queues |   <---------- | index (per lane time ranges, LOD) |  queries      | stats / find / flame /     |
| GPU collectors    |   control     | analysis engine (stats, leaks,    |               | memory / GPU views         |
+-------------------+               |  by-task rebuild, compare)        |               +---------------------------+
                                    +-----------------------------------+
```

- **The server does the heavy lifting.** Traces stay on the server; the browser asks for *what is visible* (`prof.timeline { capture, range, lanes, pixelWidth }`) and gets level-of-detail-aggregated spans (merged sub-pixel zones with counts and total time). Phones can therefore browse multi-GB captures.
- Analysis queries (statistics, find-zone histograms, flame graphs, leaks, compare) run server-side on indexed chunk files and return compact results. The same queries back the AI tools (T-PRF-*).

## Connection model

1. The *Profile* task (or any run with profiling enabled) starts the app with `IED_PROFILER=127.0.0.1:<port>;token=<t>` - the task variable `${profiler.connect}`, which the profiler plugin sets for each workspace (the token says which workspace a connection belongs to; the server names the capture when `Hello` arrives).
2. The app's profiler client connects to that address and sends `Hello` (with the token). No discovery needed in the common path.
3. `IED_PROFILER=file:<path>` makes the client write the same stream to a file instead (CI, or an app run outside IED); the profiler opens it like any `.iedtrace`. Unset, or anything the client cannot parse: profiling stays off and costs one relaxed load per event site.
4. For apps started outside IED: the client listens for nothing; instead IED listens on a well-known UDP port for `Announce` broadcasts from clients that have `IED_PROFILER=announce` and lists them in "Connect to running app" (P2).
5. If no IED is reachable, the client either buffers up to a bounded size (setting) or drops events and counts drops (reported in `Hello`/`Stats` when it connects later). Never unbounded buffering.

## ITP v1 wire format

Little-endian. All integers in events are **unsigned LEB128 varints** unless stated; signed values zig-zag encoded. Strings are referenced by interned ids.

### Framing

```
Stream  := Preamble Chunk*
Preamble:= "ITP1" (4 bytes) u16 version u16 flags
Chunk   := u32 byteLength  u8 chunkKind  varint threadId  varint baseTimeTicks  Event*
```

- `byteLength` counts the bytes after it (kind, header, events).
- `chunkKind`: `0` thread chunk (all events from one thread), `1` global chunk (definitions, memory, GC, GPU, control).
- **Times:** a `dt` field is a **zig-zag signed** varint: the event's time in ticks minus the previous timed event's in the same chunk (the first one's minus `baseTimeTicks`). The same rule in both kinds of chunk, so a global chunk, whose events come from many threads, may step back.
- Clients flush a thread's chunk when it reaches 16-64 KB or every 10 ms, so live view is smooth and per-thread ordering is preserved without a global lock.
- **Definitions before use:** a client queues every definition (`StringDef`, `SrcLocDef`, `ThreadDef`, ...) in the global queue before the event that names it, and its flusher takes the thread buffers first, then the global queue, and sends the global chunk first - so a definition always reaches the server before its first use. The server still resolves ids when it answers queries, not when it reads events, so a client that gets this wrong shows names late, never wrong.
- Optional compression (flag bit 0): each chunk body compressed with LZ4 frame format. v1 servers must accept uncompressed; compression is negotiated in `Hello`.

### Event catalog (type byte + fields)

| Type | Name | Fields | Notes |
|---|---|---|---|
| 0x01 | `Hello` | version, appName(str inline), pid, clockFreqHz, startTicks, startUnixNs, flags, token(str inline) | first event, global chunk |
| 0x02 | `Calibration` | ticks, unixNs | periodic (1 s) to correct drift |
| 0x03 | `Bye` | reason | |
| 0x04 | `Warning` | code, strId | instrumentation errors, drops |
| 0x05 | `Stats` | eventsSent, eventsDropped, bytesBuffered | periodic |
| 0x10 | `StringDef` | id, utf8(len-prefixed bytes) | ids start at 1; 0 is "none" everywhere |
| 0x11 | `SrcLocDef` | id, nameStr, functionStr, fileStr, line, color(u32, 0 = default) | |
| 0x12 | `ThreadDef` | threadId, osThreadId, groupHintStr | first time a thread appears; `threadId` is the client's small index (the chunk header's), never 0 |
| 0x13 | `ThreadName` | dt, nameStr | **rename at a time**; any number per thread |
| 0x20 | `ZoneBegin` | dt, srcLocId | |
| 0x21 | `ZoneBeginNamed` | dt, srcLocId, nameStr | per-call display name; stats can group by it (option) |
| 0x22 | `ZoneEnd` | dt | |
| 0x23 | `ZoneText` | strId | attaches to the innermost open zone |
| 0x24 | `ZoneValue` | u64 | |
| 0x25 | `ZoneColor` | color (0xRRGGBB) | |
| 0x26 | `ZoneCallstack` | callstackId | |
| 0x27 | `ZoneAllocs` | objects, bytes | what the innermost open zone allocated, its children's included; sent just before its `ZoneEnd` by a client that counts allocations per thread (C4's collector) instead of sending each one |
| 0x28 | `ZoneNative` | bytes | the native memory the innermost open zone's thread took in it (containers' buffers growing, raw blocks), its children's included; sent beside `ZoneAllocs` when it took any |
| 0x29 | `ZoneNativeFreed` | bytes | the native memory the innermost open zone's thread gave back in it, its children's included, whichever thread took it; sent beside `ZoneNative` when it gave any |
| 0x30 | `TaskBegin` | dt, taskId, nameStr, parentTaskId(0 = none) | task identity is `taskId` (unique per run, never reused); grouping key is `nameStr` |
| 0x31 | `TaskEnd` | dt, taskId | |
| 0x32 | `TaskSuspend` | dt, taskId | e.g. job yields / waits |
| 0x33 | `TaskResume` | dt, taskId | may arrive on another thread's chunk |
| 0x40 | `FrameMark` | dt, frameSetStr | continuous frames; set 0 is the default `Frame` set |
| 0x41 | `FrameBegin` / 0x42 `FrameEnd` | dt, frameSetStr | discontinuous frames |
| 0x43 | `FrameImage` | blobRef, width, height, format | reserved (P2): optional screenshots, sent through a side blob |
| 0x50 | `PlotDef` | plotId, nameStr, kind(0 number, 1 memory, 2 percent), flags | |
| 0x51 | `PlotValue` | dt, plotId, f64 (8 raw bytes) | |
| 0x52 | `Message` | dt, severity(0 trace, 1 debug, 2 info, 3 warning, 4 error, 5 fatal), strId, color | |
| 0x60 | `PoolDef` | poolId, nameStr | pool 0 = default heap |
| 0x61 | `Alloc` | dt, threadId, poolId, address, size, callstackId(0 = none) | sent in global chunks (address reuse across threads must stay ordered), so it names its thread |
| 0x62 | `Free` | dt, threadId, poolId, address | global chunks |
| 0x63 | `PoolReset` | dt, poolId | arena reset frees everything in the pool |
| 0x68 | `GcBegin` | dt, kind(0 minor, 1 major, 2 full), generation | global chunks |
| 0x69 | `GcEnd` | dt, bytesBefore, bytesAfter, objectsFreed, pinned, roots | pause = end - begin; 0 is "not known" for any field (freed bytes are then 0) |
| 0x6A | `AllocCount` | dt, objects, bytes | the process's allocations so far (cumulative); at each frame mark and periodically, for allocations per frame and per second without an event each |
| 0x6B | `HeapCensus` | dt, types | live objects by type after a collection: the next `types` events are its `HeapCensusType` lines |
| 0x6C | `HeapCensusType` | nameStr, objects, bytes | one type's live objects in the census before it |
| 0x6D | `HeapCensusTypeNative` | nameStr, objects, bytes, nativeBytes | the same with the native memory those objects hold through their containers (buffers by capacity); sent in place of `HeapCensusType` by a client that knows it (C4's census) |
| 0x70 | `LockDef` | lockId, nameStr, srcLocId | |
| 0x71 | `LockWait` / 0x72 `LockObtain` / 0x73 `LockRelease` | dt, lock (`lockId * 2 + shared`) | |
| 0x80 | `ModuleDef` | base, size, pathStr, debugIdStr (PDB GUID+age) | for server-side symbolization |
| 0x81 | `CallstackDef` | callstackId, count, addresses[] | innermost frame first; addresses resolved server-side from PDBs, or by `SymbolDef` |
| 0x82 | `Sample` | dt, threadId, callstackId, kind(0 CPU ns, 1 samples, 2 bytes, 3 objects, 4 other), weight | a call stack sample (sampling profilers, imported pprof profiles, a collector's allocation sampler); names its thread (0: the chunk's) |
| 0x83 | `SymbolDef` | address, functionStr, fileStr, line | names an address of a call stack when the client (or an importer) knows it |
| 0x90 | `GpuContextDef` | ctxId, api(0 other, 1 d3d12, 2 vulkan, 3 opengl, 4 metal), queueNameStr, periodPicoseconds | |
| 0x91 | `GpuCalibration` | ctxId, cpuTicks, gpuTicks | D3D12 `GetClockCalibration`, periodic |
| 0x92 | `GpuZoneBegin` | dt(cpu), ctxId, queryId, srcLocId | CPU-side submit time |
| 0x93 | `GpuZoneEnd` | dt(cpu), ctxId, queryId | |
| 0x94 | `GpuTime` | ctxId, queryId, gpuTicks | resolved timestamp, later |
| 0xA0 | `Annotation` | startTicks, endTicks, strId | reserved: annotations live in the trace file's index block ("Trace files") |

Server -> client control messages (same framing, global chunks): `0xE0 Configure { categoriesMask, callstackDepth, allocCallstacks }`, `0xE1 StopCapture`, `0xE2 Ping { value }`. A v1 client may ignore them: the server stops a capture by closing the connection, and a client whose send fails turns profiling off for the rest of the run.

### Rules

- Timestamps are raw client ticks; the server converts with `clockFreqHz` and the latest `Calibration` pairs (piecewise-linear between pairs).
- Zones must nest per thread. An unbalanced `ZoneEnd` produces a `Warning` on the server side and is ignored; zones still open at `Bye` are closed at the last timestamp and flagged.
- Unknown event types: the chunk's remaining bytes are skipped (each chunk is length-prefixed), and a warning is recorded - so newer clients degrade gracefully against older servers.
- `TaskResume` on a different thread than `TaskSuspend` is normal (that is the point).

## Thread names and the by-task view

- Each thread lane stores its name history `[(t0, name0), (t1, name1), ...]`. Rendering: the lane label shows the name at the left edge of the viewport; segment boundaries after the first get a subtle background tint and an inline name tag (`docs/features/12-profiler.md` F-PRF-12).
- **By-task view** (F-PRF-13) rebuilds lanes:
  1. If the capture has task events: one lane per task **name** (e.g. `Physics`, `Decompress`, `AI.Tick`), containing every interval during which a task with that name was running (between Begin/Resume and Suspend/End), with the zones that occurred on the executing thread in those intervals. Overlapping instances of the same name stack as sub-rows.
  2. Otherwise: thread-name segments become task intervals (a worker renamed to `Physics` from t1 to t2 contributes that segment to the `Physics` lane).
  3. Each slice carries a thin strip colored by OS thread (thread identity is meaningful here) with the thread name in the tooltip; "show threads" toggles back.
- Statistics and find-zone accept "group by task" to answer "how long does Physics take per frame, wherever it runs".

## GPU timing

- Per context (queue): the client records begin/end timestamp queries around GPU work, sends `GpuZoneBegin/End` at command-recording time with CPU timestamps, and later `GpuTime` when the query results are read back.
- The server maps GPU ticks to CPU time using `GpuCalibration` pairs (linear fit over a sliding window to absorb drift). D3D12 provides `ID3D12CommandQueue::GetClockCalibration`, so D3D12 contexts are always calibrated.
- GPU lanes render under the CPU lanes with connectors from the CPU submit point to the GPU execution span.

## Trace files

- Extension `.iedtrace`. Layout: the ITP stream exactly as received (so a file is replayable as a connection), followed by an index block and a footer `{ indexOffset, version, "ITPX" }`. A crash leaves a file without a footer; opening it rebuilds the index by scanning.
- The server keeps derived indexes next to the trace (`.iedtrace.idx/`: per-lane interval trees, LOD pyramids, symbol cache) - deletable caches.
- Captures live in `.ied/local/captures/` by default (per machine: the workspace's local state, `docs/07`, a folder's in the app data), with metadata (app, git commit, config, duration, size) for the capture library.
- Import converters (F-PRF-7) produce `.iedtrace` from Chrome trace JSON (Nuke `build-trace.json`, game-engine JSON profiles) and, later, Tracy files.

## Client library requirements (Bedrock `Profile::`, and any other language)

- Zero cost when disabled at compile time (Dist builds); near-zero when compiled in but not connected (one relaxed atomic load per event site).
- Per-thread lock-free buffers; a background flusher thread; global events (memory, GPU, definitions) through a small mutex-protected queue (allocation reporting is inherently ordered).
- Interning: string ids assigned on first use per connection (a hash map keyed by pointer + content hash on the client; content-based so unloading/reloading code cannot alias).
- API surface (C4 side, names indicative): `Profile::Zone("Name")` (scoped), `Profile::ZoneText`, `Profile::Frame()`, `Profile::FrameBegin/End("Set")`, `Profile::Plot("name", value)`, `Profile::Message(severity, text)`, `Profile::SetThreadName(name)` (callable any time - that is the rename), `Profile::TaskScope("Name")` / `TaskBegin/End/Suspend/Resume`, `Profile::Alloc/Free(ptr, size, pool)`, GC events wired automatically from the runtime, GPU zones (as built: `Profile::GpuContext`, `GpuZone`, `Time`, `Calibrate`; "GPU zones" under "As built").
- Thread names: C4's `Thread` and `JobSystem` call `SetThreadName` automatically ("Job Worker 3"), and `JobSystem` emits `TaskBegin/End` with the job's name when profiling is on - so C4 game engines get the by-task view for free.

## As built (2026-10-21)

**The plugin.** `ied.profiler` (`plugins/profiler`) is a built-in: its server code (`code/`, namespace `Profiler`) is compiled into the server (`src/nuke.toml`), its UI module is `ui/index.js`. It needs nothing of the host beyond the plugin API (`docs/26`): its own TCP listener, threads, topics and methods, and `IED::SetTaskVariable` / `StartTask` / `TaskEnded` / `CancelTask` for the Profile task.

- **Listener.** One loopback TCP listener per server (`profiler.port`, default 0: a free port each start). Each workspace gets a random token; `${profiler.connect}` is `127.0.0.1:<port>;token=<token>` for that workspace's tasks. A connection must send a `Hello` whose token names a workspace within 10 s, else it is closed; so is anything that is not ITP. A connection is a live capture: its bytes are written to `.ied/local/captures/<app>-<yyyyMMdd-HHmmss>.iedtrace` as they arrive and applied to the capture in memory chunk by chunk; `Bye`, a closed socket or `prof.stop` ends it, and the index block and footer are written then.
- **The capture in memory** (`capture.ch`): per thread, zones by depth (at one depth zones never overlap and arrive in order, so each depth is a sorted run a range finds by binary search) with self times computed as zones end, rename history, per-call names, text, values; per source location every zone's reference and totals; frame sets, plots, messages (kept in time order across threads), pools with every allocation and its free, collections, task runs per task name and thread, locks, GPU zones with their query times and calibrations, call stacks and symbols, samples, warnings. Names stay string ids until a query reads them. A saved capture is read back by replaying its stream; at most 6 are kept loaded (least recently used).
- **Malformed input** never ends a capture: a zone ending that never began, an address allocated twice, a task id never begun, zones nested past 255 levels, an unknown event type (the rest of its chunk is skipped), a chunk cut short - each is a warning (`prof.summary`'s `warnings`, the first 1000 kept). A free of an address the capture never saw allocated is not: a capture may begin while the program runs (a global allocator hooked mid-run frees blocks from before), so `prof.memory` counts them per pool as `earlierFrees`.
- **Trace file index block**: `"ITPI"`, u32 length, the metadata JSON (`app`, `pid`, `startUnixNs`, `durationNs`, `events`, `zones`, `frames`, `warnings`, `ended` - `bye`, `closed` or `stopped` - and `annotations`, or for an import `importedFrom` and `format`). Rewritten in place (padded, never shorter) when it changes. A file without a footer is a crash or a client's file mode: it opens by replaying everything.

**Protocol** (`docs/10` lists them): every query takes `captureId` and answers in nanoseconds since the capture started.

| Method | Params | Result |
|---|---|---|
| `prof.captures` | `wsId` | `{captures: [{captureId, path, name, live, loaded, app, sizeBytes, modifiedNs, metadata}], connect: {port, env, folder}}` |
| `prof.open` / `prof.import` | `wsId, path` | `{captureId}`: a trace file of the workspace opened; or a Chrome trace / pprof profile / `.iedtrace` converted into the library |
| `prof.stop` / `prof.close` / `prof.delete` | `captureId` | `{}`: stop recording (the connection is closed; the program goes on), unload, move the file to the trash |
| `prof.timeline` | `from?, to?, width, mode` (`threads` or `tasks`), `depth?` | `{from, to, durationNs, live, pixelNs, lanes, gpu, frames, gc, messages, messagesInRange, plots, names}`; a lane's `zones` are `[depth, start, end, nameIndex, ref]`, or `[depth, start, end, -1, count]` for a run of zones narrower than a pixel merged; task lanes carry `slices` `[start, end, track, row]`; a thread lane's `locks` are its lock rows `{lock, name, items}`, an item `[start, end, state, hold]` (state 0 held, 1 held while others waited, 2 waited) or `[start, end, worstState, -count]` for a merged run, at most 12 rows a lane (`locksHidden` the rest); `renames` only when they are not denser than one per 4 pixels |
| `prof.stats` | `from?, to?, groupBy` (`zone`, `thread`, `task`), `order`, `filter`, `limit` | `{rows: [{name, site, function, file, line, group, count, totalNs, selfNs, meanNs, medianNs, p95Ns, minNs, maxNs, share}], rowCount}` |
| `prof.findZone` | `name` or `site`, `from?, to?, bins, low?, high?, limit` | the count, mean, median, p95, min, max, a histogram (log bins when the durations span decades) and the slowest instances |
| `prof.zone` | `ref` (`track:depth:index`) | one zone's details: site, times, self, thread, task, text, value, color, call stack, frame, parents, children by name, what happened inside it (`inside`, below) |
| `prof.flame` | `from?, to?, track?, samples?` | `{source: zones|samples, unit, nodes, root: {name, total, self, count, children}}` |
| `prof.frames` / `prof.frame` | `width` / `index?` (-1 or none: the worst) | the frame graph's bars (the worst frame of each run when there are more frames than pixels) with mean, median, p95, p99, max; one frame's threads, costliest zones, collections and GPU work |
| `prof.memory` | `width` | pools over time with peak, allocations and frees; the plots of bytes (kind 1: a collected program's GC heap and process memory) with their last value, peak and usage; GC totals (`freedBytes` -1 when no collection said what the heap held before) |
| `prof.messages` | `from?, to?, severity, text, offset, limit` | messages in time order |
| `prof.locks` | `from?, to?, order` (`wait`, `maxWait`, `contended`, `hold`, `acquisitions`, `name`), `limit` | `{locks, rowCount, rows: [{lock, name, function, file, line, acquisitions, contended, blocked, waitNs, maxWaitNs, meanWaitNs, holdNs, maxHoldNs, contention, threads, stuck, worst}]}`; `worst` is the longest wait, `{track, index, thread, fromNs, toNs}` |
| `prof.lockHold` | `lock, track, index` | one hold of a lock on a thread: its state (`held`, `contended`, `waiting`), its wait and hold, the lock's source, other threads' holds while it waited (`heldBy`) and their waits while it held (`waitedBy`), the lock's totals |
| `prof.annotations` / `prof.annotate` / `prof.updateAnnotation` / `prof.deleteAnnotation` | - / `from, to?, text` / `id, text?, from?, to?` / `id` | `{annotations: [{id, fromNs, toNs, text, by}]}` in time order / `{annotation}` / `{annotation}` / `{}` |
| `prof.summary` / `prof.compare` | - / `a, b, limit` | a capture at a glance; two captures' frame times, memory (allocations in all and per frame, peak, collections) and the zones whose total changed most |
| `prof.allocations` | `from?, to?, groupBy` (`zone`, `path`, `thread`, `pool`, `size`, `callstack`, `function` - a stack's innermost frame; call stacks when the allocations have them, else zones), `source?` (`events`, `zones`, `samples`; the best the capture has when left out - a counted capture's samples when grouped by call stack or function), `order` (`count`, `bytes`), `limit` | `{source: events|zones|samples|none, count, bytes, perSecond, churn, churnWindow, live, liveBytes, frames: {count, meanCount, medianCount, p95Count, maxCount, maxIndex, meanBytes}, groups: [{label, file, line, callstack, count, bytes, meanSize, perFrame, bytesPerFrame, churn, live, thread, threads}]}`; from samples `{unit, samples, weight, groups: [{label, file, line, weight, share}]}`; from zones also `samples`, the allocation samples there are beside the counts, `inZoneNativeBytes`, `inZoneNativeFreed` and each group's `nativeBytes`, `nativeBytesPerFrame`, `nativeFreed` (the native memory its zones took and gave back; `bytes` order sorts by bytes and native taken) |
| `prof.leaks` | `at?, from?, to?, groupBy, limit` | allocations live at `at` (the end) made in `[from, to)`, by group, the most bytes first: each with `firstNs`, `lastNs`, `spread` (in how many tenths of the window they were made), `perSecond` and `accumulating` |
| `prof.memoryTrend` | `from?, to?, points` | `{series: [{kind: pool|plot, name, start, end, perSecond, rises, steps, peak, peakNs, floorStart, floorEnd, floorPeak, floorPeakNs, floorRises, values, floors}], gc: {count, perSecond, pauseNs, inSlowFrames, heapAfter, longest}, teardownNs}` |
| `prof.plots` | `name?, from?, to?, points` | the counters (or the plots whose name holds `name`): min, mean, max, last, values at points |
| `prof.census` | `from?, to?, limit` | `{censuses, firstNs, lastNs, typeCount, native, types: [{name, firstObjects, lastObjects, firstBytes, lastBytes, firstNativeBytes, lastNativeBytes, peakBytes, rises, steps, accumulating}]}`, the types that grew most first (bytes and native memory held together) |
| `prof.hitches` | `threshold?, limit` | `{frames, medianNs, thresholdNs, slow, worstNs, list: [{index, startNs, durationNs, overNs, frameThread, frameThreadBusyNs, zones: [{name, thread, ref, file, line, selfNs, usualNs}], gc, waits, allocations, messages}], causes: [{kind: zone|gc|lock|allocations, name, thread, frames, meanNs}]}` |
| `prof.threads` | `from?, to?` | `{meanBusyThreads, threads: [{track, name, marksFrames, busyNs, share, zones, longestIdleNs, longestIdleAtNs, waitNs, top}]}` |
| `prof.window` | `from, to, depth, thread?, limit` | `{threads: [{name, track, entries: [{kind: zone|wait|message, startNs, ...}]}], gc, left}`; `prof.zone` gains `inside` (allocations - its children's included, and its own as `selfCount`, `selfBytes`; the native memory taken and given back in it, `nativeBytes`, `nativeFreed` and `selfNativeBytes`, `selfNativeFreed` - lock waits, messages and collections in the zone) |

Topics: `profs:<wsId>` (snapshot: `prof.captures`'s answer; event `captures.changed`) and `prof:<captureId>` (snapshot `{captureId, live, app, durationNs, events, zones, bytes, warnings}`; events `stats` four times a second while live, `ended`, and `annotations` with every note whenever one changes). Both are pushed by the plugin.

**The UI** (mockup 06): the Profiler activity is a dock host with the Captures tool window (live first; how a program connects; Import); a capture opens as a document (`prof:<captureId>` tab kind) - the toolbar (recording state, Pause view / Follow and Stop while live, Threads or Tasks, Show all, the tool windows, Ask AI), the frame time graph (a click zooms to a frame, a drag to a run), the canvas timeline (`ui/timeline.js`, `timelinedraw.js`; theme tokens read once a paint), the zone details beside it, and the bottom tool windows Statistics (CSV), Find Zone, Messages, Memory and Flame Graph over the selection, else the view (Messages and Memory over the whole capture). Memory draws each pool and memory plot in its own lane, scaled to its own range (a small heap's sawtooth beside a large process's climb); for a program that sends each allocation it lists those live at the end (or the selection's end), for one that counts them (a collected program) what was allocated by zone, zone path or thread with its source, and the types its censuses found, those that keep accumulating marked. The zone details say what happened in the zone: what it allocated (its own share too), its lock waits, the collections that stopped it and its messages. A live capture follows its newest data until the timeline is moved. On a phone the details are a sheet under the timeline and a tool window takes the whole view; a pick in a tool window (a lock, a note, an instance, a message) closes it to show the timeline. The status bar says "Capturing" while a program of the workspace records. Ask AI starts a chat with the question and an excerpt naming the capture, so the AI can use its tools on it.

**Locks** (F-PRF-18). A lock is defined once (`LockDef`: its name and source location); its waits, obtains and releases are events of the thread that does them. The server keeps, per lock and thread, the holds (`{wait, obtain, release, shared}`; a recursive lock's inner holds are one hold) and a wait not yet granted - still waiting when the capture ends is a deadlock or a stuck thread, drawn to the end and counted as `stuck`. A hold is contended when another thread was waiting as it was released. On the timeline each thread's locks are rows under its zones: held (the quiet text color), held while others waited (warn) and waited (error, a thinner bar), merged into runs narrower than a pixel like zones; a click selects the hold, and the details say how long it waited and held, who held the lock while it waited and who waited while it held, and the lock's totals. The Locks tool window lists each lock used in the selection or view - waits, time waited, the longest wait, contended holds, holds, time held, threads, its source - sorted on the server by any of them; a row zooms to and selects the lock's longest wait.

**Notes** (F-PRF-19). A note is a range of the capture (or a moment) with text, by the owner or by the AI (`by`: `owner`, `ai`). Notes live in the trace file's metadata (`annotations`), so they travel with the file: a live capture's are written when it ends, a finished file's index block is rewritten, and a file without one (a client's file mode, a crash) gets an index appended once it has been quiet for 10 s. A change is pushed as `annotations` on `prof:<captureId>`, so every client shows it at once. The timeline draws them on a Notes row under the frames, each a faint band over the lanes (the owner's in the info color, the AI's in its own); Note in the toolbar adds one on the selected range, zone or lock hold; the Notes tool window lists them with edit and delete, and a click zooms to the note's range. Times are written as precisely as a note's length needs ("1290.05 ms to 1290.45 ms").

**Finding problems** (`code/investigate.ch`; T-PRF-13 to T-PRF-20). Analyses that answer what is wrong rather than what happened, each a `prof.*` method and a `profile_*` tool:

- *Where it allocates* (`allocations.cf`). Live clients send no call stacks with allocations, so an allocation is placed by the zone its thread was in - the innermost (`zone`) or the path of zones down to it by site name (`path`); with call stacks, by stack. Per group: allocations and bytes in all and per frame (over the frames that began in the range), mean size, **churn** - blocks freed within the frame they were made in (10 ms without frames), the garbage a frame leaves - and what is still live at the range's end. A capture with no allocation events reads its allocation samples (kind bytes or objects: a pprof heap profile, the Go client) by the function of each stack's first frame.
- *What a collected program allocates and keeps* (`zoneallocs.cf`, `census.cf`). A client that counts allocations instead of sending each one - C4's collector - sends each zone's count (`ZoneAllocs`, and `ZoneNative` for the native memory its thread took; its children's included, which the server takes away to give each zone its own), the process's count at each frame (`AllocCount`) and, after a collection, live objects by type (`HeapCensus`), with allocations sampled with their call stacks (`Sample`, kind bytes). `prof.allocations` then answers by zone, path or thread from the counts (`source: zones`: no churn or what stays live, a collected heap's garbage being found later), with the process's count per frame, and by function or call stack from the samples; a slow frame's allocations come from the same counts. A type whose live bytes (with the native memory they hold, when the census says) rose after 70% of the collections, by 20% and by 64 KiB or 500 objects, **keeps accumulating**: in a collected heap a leak is objects still reachable, and a small object that owns a buffer weighs the buffer. `profile_diagnose`'s allocation finding names the functions the samples say allocated most, and its growth finding the zones that took the most native memory.
- *What it keeps* (`memory.cf` Leaks). Allocations live at a time, made in a window, grouped as above; a group whose live allocations were made in 6 or more of the window's tenths (and number at least 10) **keeps accumulating** - a leak - while one made in a single tenth was made at once (a cache, a load). A program that frees most of its memory as it exits would show nothing live at the end: the tool reads just before that **teardown** - the last time the largest pool was at its high in the capture's last tenth, when it then dropped below a quarter of it (and held at least 1 MiB).
- *How memory moves* (`trend.cf`). Each pool and memory plot read at evenly spaced points; its **floor** is the lowest it got in each step - for a collected heap about what stayed alive - so a floor that keeps rising is memory kept, a high peak over a flat floor is churn. The floor's highest step is kept as well, since a teardown ends the series low. Collections: how often, their pauses, the heap left after each, and how many fell in slow frames.
- *Why frames are slow* (`hitches.cf`). A frame is slow past `threshold`, else twice the median. In each, every zone's self time per thread is set against what it takes in a frame on average - as it is on the thread that marks the frames (`FrameSet.track`, the thread of its first mark), in proportion to the frame's length on other threads, since a worker running all the time does more of it in a longer frame without slowing it. The zones far past their usual time (over 1% of the frame), collections, lock waits, allocations over twice the usual and warnings are its causes, then counted over all slow frames. How long the frame's thread was busy in it says whether the code or a wait made it slow.
- *How busy the threads are* (`threads.cf`): busy time (inside top-level zones), the longest idle gap, lock waits and costliest zones per thread; threads busy at once on average.
- *A window and a zone* (`window.cf`): zones to a depth with their allocations, lock waits and messages per thread in time order; inside one zone, the allocations it made, the locks it waited for, what it logged and the collections that stopped it.
- *Diagnose* (`tooldiagnose.cf`, text only): the problems held to thresholds - slow frames (high at 5% of frames or a worst frame 4x the median, medium at 1% or 3x); memory whose floor rose in 7 of 9 steps by 1 MiB and either 10% or 4 MiB, or a heap left after collections that grew 20% (high), with the groups that keep accumulating (100 allocations or more), the series that stayed flat, and a collected heap whose collections leave it small (the growth is outside its objects); a lock whose waits are 1% of the capture or 5% of a waiting thread's busy time (high at 5% or 20%, or a wait still open at the end); 100 allocations or 64 KiB a frame (high at 1000 or 1 MiB; 50,000 a second without frames); collection pauses over 2% of the capture or 3 in slow frames; the zone that takes 25% of the frame thread's busy time; one thread busy 75% while others idle under 25%; dropped events and warnings - most costly first, each with its evidence and the tool that looks closer.

**The AI** (`docs/13` T-PRF): `profile_list`, `profile_summary`, `profile_diagnose`, `profile_top_zones`, `profile_frame`, `profile_hitches`, `profile_threads`, `profile_zone`, `profile_timeline`, `profile_find_zone`, `profile_flame`, `profile_messages`, `profile_allocations`, `profile_leaks`, `profile_memory`, `profile_plots`, `profile_compare`, `profile_locks`, `profile_annotate` and `profile_capture` are C4 functions that answer through the plugin's `profiler.query` server binding as compact text (times with units, points in time in milliseconds as precise as `from_ms` takes them, aligned tables, an indented call tree); `profile_summary` adds the three busiest locks and the capture's notes and points at `profile_diagnose`, `profile_annotate` leaves a note the owner sees on the timeline; `profile_capture` runs the startup project's Profile task (`IED::StartTask`), waits for its program to connect, records for the asked seconds, stops the capture and cancels the run. The `profiler` skill says how to instrument a program in each language.

**The Profile task.** A task in the `profile` slot starts the program with `IED_PROFILER=${profiler.connect}`; the `nuke` preset has one (`plugins/nuke`, inherited by `cpp.nuke`, `go.nuke` and `rust.nuke`), and so do the `go` preset (`go run .`) and `rust.cargo` (`cargo run`, its Debug / Release profile). Debug > Start Profiling (Alt+F2) notes the workspace's captures, runs the task, and opens the first new live capture of that workspace when its program connects.

**Clients.** Bedrock `Profile::` (`C4/Bedrock/Profile/`, opt-in `include "profile.ch"`): zones as scope guards over a static `Site` (the `PROFILE_ZONE("Name")` macro records the file and line) or a name looked up at run time, frames, plots, messages, thread names at any time, tasks, pools' allocations, locks (`PROFILE_MUTEX(queue, "Queue")` is a `Profile::Mutex`, a `Threading::Mutex` whose waits and holds are recorded; a `Profile::LockSite` with `LockWait`, `LockObtained` and `LockReleased` reports a lock of the program's own), every `JobSystem` job as a task named as it was submitted (through `Threading::SetJobObserver` while a session runs), and each time a collection stopped the program (`GC::LastPauses` from the collection callback: one stop for `GC::Collect`, several for a concurrent cycle, timed on the system's counter and mapped onto the session's clock) with the GC heap, the process's private bytes and the runtime's native memory in use (`GC::Stats.nativeBytes`) plotted ten times a second. Allocations are counted, not sent: each zone sends what its thread allocated in it (`ZoneAllocs` from `GC::ThreadAllocated`, a lock-free read) and the native memory it took and gave back (`ZoneNative`, `ZoneNativeFreed`: containers' buffers growing, raw blocks), what the profiler's own work took meanwhile left out and each frame mark the process's count (`AllocCount` from `GC::Allocated`). While a session runs the collector takes a census after each collection (`GC::SetCensus`; a `HeapCensus` whose lines say what native memory each type's objects hold, `HeapCensusTypeNative`, objects made before it as "(type not known)") and samples about one allocation in every 64 KiB (`GC::SetAllocSampling`), each a `Sample` of its thread with its call stack and each address named by the reflected function holding it (`Reflection::FunctionContaining`) and the line of its call (`Platform::SourceLineOf`, a program built with -g); both are put back when the session stops. The census makes each `new` dearer while on (about 16 ns in the LeakyC4 demo at -O2). `SetThreadName` names the system's thread too (debuggers and dumps show it), and a thread the system already knows by a name starts with that name. Events are stamped with `rdtsc`, its frequency measured against the system counter at start (`Hello` flags it as estimated; `Calibration` pairs every second correct it on the server). Each thread writes encoded events into its own buffer under an uncontended lock; a flusher thread sends them every 10 ms. One relaxed load per call when no session runs; `-D PROFILE=0` compiles the macros away. A send that fails ends the session. Tested by `C4/Bedrock/Tests/profile_tests.cf` (a session to a file read back event by event).

**GPU zones** (Bedrock `Profile/gpu.ch`, B72). A `Profile::GpuContext` is one queue of the program's renderer (its
API, name, tick period in picoseconds and the size of its query ring), defined (`GpuContextDef`) the first time a
session hears of it. Bedrock records no GPU commands: `BeginZone(site)` sends `GpuZoneBegin` at the CPU time of
recording and hands out two query slots round the ring (begin `q`, end `q + 1`, from an atomic counter, so threads
recording command lists in parallel never share one); the program writes its timestamps there, `EndZone(q)` sends
`GpuZoneEnd` with `q + 1`, and once the GPU is done `Time(slot, ticks)` sends each `GpuTime`; `Calibrate` sends
`GpuCalibration` from D3D12's `GetClockCalibration`, its CPU reading put on the session's clock. The renderer emits the
API's own markers (PIX events) with the zone's site name. The server (`ingest.cf`) nests GPU zones per recording
thread, gives each zone the times of its own slots - a slot is used again round the ring, so a time goes to the zone
waiting for it when it arrives, or, read back before its zone reached the server (another thread's chunk of the same
flush), to the next zone that takes the slot - and puts them on the CPU's timeline through the calibration; the
timeline's GPU lane, a frame's GPU work and the summary read them.

**C and C++** (`plugins/cpp/libraries/Profiling/src/Profiling/Itp.h`, `Itp.cpp`): the same client in two files that need only the C++ standard library and the system's sockets (Windows or POSIX), so any C or C++ program can copy them. `ITP_ZONE("Name")` (a C++ scope guard; `ITP_ZONE_BEGIN`/`ITP_ZONE_END` in C), named zones with text and a value, `itp_frame`, `itp_plot`, `itp_message`, `itp_thread_name`, tasks, pools, locks (`ITP_LOCKABLE(std::mutex, queue, "Queue")` is an `itp::Lockable` over any lockable type, shared locks too; in C an `itp_lock` with `itp_lock_wait`, `itp_lock_obtained` and `itp_lock_released`); `itp_start`/`itp_stop` for tools, else the first call starts from `IED_PROFILER` and the session ends at exit. `itp_alloc`/`itp_free` may be called from a replaced `operator new`: the client's own allocations are not reported. `ITP_ENABLE=0` compiles the macros away. Frost's `Performance.h` macros, its logger (`itp_message`) and its memory tracker report through it; it replaced the Tracy client (the submodule and the Tracy GUI setup package are gone). Tested by `tools/pylib/tests/test_itp_client.py`: built with the vendored clang, run to a file and to a listener, read back with `itp.py`.

**Go** (`plugins/go/profiler`, the module `github.com/AlanBuechner/IED/plugins/go/profiler`, standard library only): each goroutine records on a track of its own - the one `WithThread(ctx, name)` puts in a context, else "goroutine N" made the first time it records without one (found from its stack, about a microsecond). `defer profiler.Zone(ctx, "Name").End()` is at the caller's file, line and function (a call site's first name; another name at the same call is a named zone), with text and a value; frames, plots, messages, tasks that move between goroutines (`Suspend`, `Resume(ctx)`), pools, locks (`profiler.NewMutex("Queue")` and `NewRWMutex`, whose `Lock(ctx)` and `Unlock(ctx)` record on ctx's track). The runtime is reported ten times a second: each collection's stop-the-world pauses (`debug.ReadGCStats` keeps when each ended and how long it took), the Go heap, the goroutine count, and the runtime's own allocation profile (`runtime.MemProfile`, sampled every 64 KiB while a session runs unless the program set `MemProfileRate`) as allocation samples - the bytes each call stack allocated since the last report, its first frame the program's code (the runtime's frames left off, the profiler's own allocations not sent); the runtime publishes them at each collection, so they are as late as the last one. On amd64 events are stamped with the time stamp counter (an assembly function), its rate measured at start against QueryPerformanceCounter on Windows and the monotonic clock elsewhere: Go's own clock on Windows moves only at the timer interrupt (about 0.5 ms), too coarse for zones. Go runs nothing at exit, so `defer profiler.Stop()` in main sends the last events. Tested by its `go test` and `tools/pylib/tests/test_itp_go.py` (its example to a file and to a listener).

**Rust** (`plugins/rust/profiler`, the `ied-profiler` crate, std only): `let _zone = ied_profiler::zone!("Name");` (a static site with the enclosing function's path, the file and line; `zone!(site, name)` for a name given per call), frames, plots, messages, tasks, pools, locks (`ied_profiler::Mutex::new("Queue", value)`, or `mutex!("Queue", value)` in a static, a std mutex whose guard records the release; `LockSite` for a lock of the program's own); threads keep the names std gives them ("main", a `Builder::name`). `Tracked<A>` wraps the global allocator and reports every block - the client's own allocations are not reported, so it neither recurses nor deadlocks. The `tracing` feature's `ied_profiler::tracing::Subscriber` makes spans zones (their fields the zone's text) and events messages of their level, so an async task's polls are zones on the threads that ran them; it is a whole subscriber (a `tracing-subscriber` layer that composes with others is not built). The clock is `Instant` (QueryPerformanceCounter on Windows). The session ends at exit (`atexit`). Tested by `cargo test` and `tools/pylib/tests/test_itp_rust.py` (both examples).

**Importers** (`code/importers.cf`): the Chrome trace event format (`X`, `B`/`E`, instants, counters, async `b`/`e` as tasks, thread and process names; Rust's `tracing-chrome`, Chrome, Perfetto JSON, Nuke's build trace) and pprof (gzip and protobuf decoded in C4; Go's `runtime/pprof`, Rust's `pprof-rs`, gperftools): samples with their stacks and symbols, flame graph and statistics from them.

**Importers of other plugins** (`profilerImporters`, `code/contributed.cf`): an entry names the extensions it takes, the text a file must begin with (`magic`) and a command (`run`) that writes `${output}` from `${input}` (`${pluginFolder}` and `${config:<key>}` expand too) as Chrome trace JSON, pprof or an `.iedtrace`. `prof.import` runs it in the workspace's root (a trusted workspace; 600 s, or the entry's `timeoutSeconds`; its output is the error when it fails), reads what it wrote and names the capture after the file imported, the importer's id as its metadata's `format`. A file without the entry's `magic` goes to the profiler's own readers. The go plugin's `go.trace` reads Go execution traces (`runtime/trace`, `go test -trace`) with the toolchain that wrote them (`plugins/go/importer`): `go tool trace`'s viewer, run headless, gives each P's goroutines, the collector's phases and the runtime's counters; its parsed events (`-d=parsed`) give each goroutine a track with its regions as zones (those outside any task too, which the viewer's JSON leaves out), its tasks and its logs. The go plugin's Solution Explorer menu offers Import into Profiler on `.out` and `.trace` files.

**Checks**: 53 unit tests beside the code (`*_tests.cf`; `locks_tests.cf`, `annotations_tests.cf`, `investigate_tests.cf` and `counted_tests.cf` among them), `tools/server-checks/tests/test_profiler.py` (19: every investigation tool and method on a troubled game's capture, a collected program's counts and censuses, live capture to file, queries, restart, refused connections, warnings, imports, the AI's answers, `${profiler.connect}`, `profile_capture`, a contributed importer converting, failing, passed over and refused in an untrusted workspace, a real Go execution trace, a lock's waits and holds, notes kept in the file across a restart, a live capture's notes written when it ends), the browser unit tests `frontend/tests/profiler.test.js`, the e2e scenarios `profiler_live_capture`, `profiler_locks_and_notes` and `profiler_collected_memory` (desktop and phone), `tools/pylib/tests/test_itp_client.py`, `test_itp_go.py` and `test_itp_rust.py` (each client's stream read back with the same checks, `itp_clients.py`), and `tools/pylib/itp.py`, the protocol's reference codec in Python.

## Open items

- LZ4 in the client means a C dependency in Bedrock; v1 can ship uncompressed (localhost bandwidth is plentiful) and add compression with the first remote-capture use case.
- Not built yet: Go CPU samples while live (pprof is imported afterwards), a `tracing-subscriber` layer for Rust, GPU zones from the C and C++, Go and Rust clients (Bedrock's `Profile::GpuContext` sends them), a lock-order graph (which locks are taken inside which, to find inversions before they deadlock), symbolizing raw addresses from PDBs, UDP announce, captures larger than memory (indexes on disk), the debugger's Diagnostic Tools graphs (F-PRF-63).
- Sampling and context switches (ETW) are P2 and outside ITP v1 (the server collects them directly via an ETW helper and merges by timestamp).
