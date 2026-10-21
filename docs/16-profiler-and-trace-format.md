# 16 - Profiler design and trace protocol

## Purpose

Resolve `docs/99` #9: define the seam between instrumented programs (C4 apps through Bedrock `Profile::`, game engines, anything else) and the IED profiler - the **IED Trace Protocol (ITP) v1** - and the profiler's server/client architecture. Feature list: `docs/features/12-profiler.md`.

Tracy (the client sources are the `C4/Frost/Projects/Profiling/tracy` submodule, used by the compiler's C++ libraries) is a **feature reference only** for IED; it is never linked into IED. ITP deliberately fixes the Tracy limitations that matter for this project:

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
| 0x69 | `GcEnd` | dt, bytesBefore, bytesAfter, objectsFreed, pinned, roots | pause = end - begin |
| 0x70 | `LockDef` | lockId, nameStr, srcLocId | |
| 0x71 | `LockWait` / 0x72 `LockObtain` / 0x73 `LockRelease` | dt, lock (`lockId * 2 + shared`) | |
| 0x80 | `ModuleDef` | base, size, pathStr, debugIdStr (PDB GUID+age) | for server-side symbolization |
| 0x81 | `CallstackDef` | callstackId, count, addresses[] | addresses resolved server-side from PDBs |
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
- Captures live in `.ied/local/captures/` by default (per machine), with metadata (app, git commit, config, duration, size) for the capture library.
- Import converters (F-PRF-7) produce `.iedtrace` from Chrome trace JSON (Nuke `build-trace.json`, game-engine JSON profiles) and, later, Tracy files.

## Client library requirements (Bedrock `Profile::`, and any other language)

- Zero cost when disabled at compile time (Dist builds); near-zero when compiled in but not connected (one relaxed atomic load per event site).
- Per-thread lock-free buffers; a background flusher thread; global events (memory, GPU, definitions) through a small mutex-protected queue (allocation reporting is inherently ordered).
- Interning: string ids assigned on first use per connection (a hash map keyed by pointer + content hash on the client; content-based so unloading/reloading code cannot alias).
- API surface (C4 side, names indicative): `Profile::Zone("Name")` (scoped), `Profile::ZoneText`, `Profile::Frame()`, `Profile::FrameBegin/End("Set")`, `Profile::Plot("name", value)`, `Profile::Message(severity, text)`, `Profile::SetThreadName(name)` (callable any time - that is the rename), `Profile::TaskScope("Name")` / `TaskBegin/End/Suspend/Resume`, `Profile::Alloc/Free(ptr, size, pool)`, GC events wired automatically from the runtime, `Profile::Gpu::Context(...)`, `Profile::Gpu::Zone(ctx, cmdList, "Name")`, `Profile::Gpu::Collect(ctx)`.
- Thread names: C4's `Thread` and `JobSystem` call `SetThreadName` automatically ("Job Worker 3"), and `JobSystem` emits `TaskBegin/End` with the job's name when profiling is on - so C4 game engines get the by-task view for free.

## Open items

- LZ4 in the client means a C dependency in Bedrock; v1 can ship uncompressed (localhost bandwidth is plentiful) and add compression with the first remote-capture use case.
- Sampling and context switches (ETW) are P2 and outside ITP v1 (the server collects them directly via an ETW helper and merges by timestamp).
