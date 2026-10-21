# 16 - Profiler design and trace protocol

## Purpose

Resolve `docs/99` #9: define the seam between instrumented programs (C4 apps through Bedrock `Profile::`, game engines, anything else) and the IED profiler - the **IED Trace Protocol (ITP) v1** - and the profiler's server/client architecture. Feature list: `docs/features/12-profiler.md`.

Tracy is a **feature reference only** for IED; it is linked nowhere (the compiler's C++ libraries profile through the C/C++ ITP client, `C4/Frost/Projects/Profiling/src/Profiling/Itp.h`). ITP deliberately fixes the Tracy limitations that matter for this project:

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
| 0x69 | `GcEnd` | dt, bytesBefore, bytesAfter, objectsFreed, pinned, roots | pause = end - begin; 0 is "not known" for any field (freed bytes are then 0) |
| 0x70 | `LockDef` | lockId, nameStr, srcLocId | |
| 0x71 | `LockWait` / 0x72 `LockObtain` / 0x73 `LockRelease` | dt, lock (`lockId * 2 + shared`) | |
| 0x80 | `ModuleDef` | base, size, pathStr, debugIdStr (PDB GUID+age) | for server-side symbolization |
| 0x81 | `CallstackDef` | callstackId, count, addresses[] | innermost frame first; addresses resolved server-side from PDBs, or by `SymbolDef` |
| 0x82 | `Sample` | dt, threadId, callstackId, kind(0 CPU ns, 1 samples, 2 bytes, 3 objects, 4 other), weight | a call stack sample (sampling profilers, imported pprof profiles); names its thread |
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
- Captures live in `.ied/local/captures/` by default (per machine), with metadata (app, git commit, config, duration, size) for the capture library.
- Import converters (F-PRF-7) produce `.iedtrace` from Chrome trace JSON (Nuke `build-trace.json`, game-engine JSON profiles) and, later, Tracy files.

## Client library requirements (Bedrock `Profile::`, and any other language)

- Zero cost when disabled at compile time (Dist builds); near-zero when compiled in but not connected (one relaxed atomic load per event site).
- Per-thread lock-free buffers; a background flusher thread; global events (memory, GPU, definitions) through a small mutex-protected queue (allocation reporting is inherently ordered).
- Interning: string ids assigned on first use per connection (a hash map keyed by pointer + content hash on the client; content-based so unloading/reloading code cannot alias).
- API surface (C4 side, names indicative): `Profile::Zone("Name")` (scoped), `Profile::ZoneText`, `Profile::Frame()`, `Profile::FrameBegin/End("Set")`, `Profile::Plot("name", value)`, `Profile::Message(severity, text)`, `Profile::SetThreadName(name)` (callable any time - that is the rename), `Profile::TaskScope("Name")` / `TaskBegin/End/Suspend/Resume`, `Profile::Alloc/Free(ptr, size, pool)`, GC events wired automatically from the runtime, `Profile::Gpu::Context(...)`, `Profile::Gpu::Zone(ctx, cmdList, "Name")`, `Profile::Gpu::Collect(ctx)`.
- Thread names: C4's `Thread` and `JobSystem` call `SetThreadName` automatically ("Job Worker 3"), and `JobSystem` emits `TaskBegin/End` with the job's name when profiling is on - so C4 game engines get the by-task view for free.

## As built (2026-10-21)

**The plugin.** `ied.profiler` (`plugins/profiler`) is a built-in: its server code (`code/`, namespace `Profiler`) is compiled into the server (`src/nuke.toml`), its UI module is `ui/index.js`. It needs nothing of the host beyond the plugin API (`docs/26`): its own TCP listener, threads, topics and methods, and `IED::SetTaskVariable` / `StartTask` / `TaskEnded` / `CancelTask` for the Profile task.

- **Listener.** One loopback TCP listener per server (`profiler.port`, default 0: a free port each start). Each workspace gets a random token; `${profiler.connect}` is `127.0.0.1:<port>;token=<token>` for that workspace's tasks. A connection must send a `Hello` whose token names a workspace within 10 s, else it is closed; so is anything that is not ITP. A connection is a live capture: its bytes are written to `.ied/local/captures/<app>-<yyyyMMdd-HHmmss>.iedtrace` as they arrive and applied to the capture in memory chunk by chunk; `Bye`, a closed socket or `prof.stop` ends it, and the index block and footer are written then.
- **The capture in memory** (`capture.ch`): per thread, zones by depth (at one depth zones never overlap and arrive in order, so each depth is a sorted run a range finds by binary search) with self times computed as zones end, rename history, per-call names, text, values; per source location every zone's reference and totals; frame sets, plots, messages (kept in time order across threads), pools with every allocation and its free, collections, task runs per task name and thread, locks, GPU zones with their query times and calibrations, call stacks and symbols, samples, warnings. Names stay string ids until a query reads them. A saved capture is read back by replaying its stream; at most 6 are kept loaded (least recently used).
- **Malformed input** never ends a capture: a zone ending that never began, a free of an unknown address, an address allocated twice, a task id never begun, zones nested past 255 levels, an unknown event type (the rest of its chunk is skipped), a chunk cut short - each is a warning (`prof.summary`'s `warnings`, the first 1000 kept).
- **Trace file index block**: `"ITPI"`, u32 length, the metadata JSON (`app`, `pid`, `startUnixNs`, `durationNs`, `events`, `zones`, `frames`, `warnings`, `ended` - `bye`, `closed` or `stopped` - and `annotations`, or for an import `importedFrom` and `format`). Rewritten in place (padded, never shorter) when it changes. A file without a footer is a crash or a client's file mode: it opens by replaying everything.

**Protocol** (`docs/10` lists them): every query takes `captureId` and answers in nanoseconds since the capture started.

| Method | Params | Result |
|---|---|---|
| `prof.captures` | `wsId` | `{captures: [{captureId, path, name, live, loaded, app, sizeBytes, modifiedNs, metadata}], connect: {port, env, folder}}` |
| `prof.open` / `prof.import` | `wsId, path` | `{captureId}`: a trace file of the workspace opened; or a Chrome trace / pprof profile / `.iedtrace` converted into the library |
| `prof.stop` / `prof.close` / `prof.delete` | `captureId` | `{}`: stop recording (the connection is closed; the program goes on), unload, move the file to the trash |
| `prof.timeline` | `from?, to?, width, mode` (`threads` or `tasks`), `depth?` | `{from, to, durationNs, live, pixelNs, lanes, gpu, frames, gc, messages, messagesInRange, plots, names}`; a lane's `zones` are `[depth, start, end, nameIndex, ref]`, or `[depth, start, end, -1, count]` for a run of zones narrower than a pixel merged; task lanes carry `slices` `[start, end, track, row]`; `renames` only when they are not denser than one per 4 pixels |
| `prof.stats` | `from?, to?, groupBy` (`zone`, `thread`, `task`), `order`, `filter`, `limit` | `{rows: [{name, site, function, file, line, group, count, totalNs, selfNs, meanNs, medianNs, p95Ns, minNs, maxNs, share}], rowCount}` |
| `prof.findZone` | `name` or `site`, `from?, to?, bins, low?, high?, limit` | the count, mean, median, p95, min, max, a histogram (log bins when the durations span decades) and the slowest instances |
| `prof.zone` | `ref` (`track:depth:index`) | one zone's details: site, times, self, thread, task, text, value, color, call stack, frame, parents, children by name |
| `prof.flame` | `from?, to?, track?, samples?` | `{source: zones|samples, unit, nodes, root: {name, total, self, count, children}}` |
| `prof.frames` / `prof.frame` | `width` / `index?` (-1 or none: the worst) | the frame graph's bars (the worst frame of each run when there are more frames than pixels) with mean, median, p95, p99, max; one frame's threads, costliest zones, collections and GPU work |
| `prof.memory` / `prof.leaks` | `width` / `at?, groupBy` (`callstack`, `pool`, `thread`) | pools over time with peak, allocations and frees, GC totals; allocations live at a time, grouped, largest first |
| `prof.messages` | `from?, to?, severity, text, offset, limit` | messages in time order |
| `prof.summary` / `prof.compare` | - / `a, b, limit` | a capture at a glance; two captures' frame times and the zones whose total changed most |

Topics: `profs:<wsId>` (snapshot: `prof.captures`'s answer; event `captures.changed`) and `prof:<captureId>` (snapshot `{captureId, live, app, durationNs, events, zones, bytes, warnings}`; events `stats` four times a second while live, `ended`). Both are pushed by the plugin.

**The UI** (mockup 06): the Profiler activity is a dock host with the Captures tool window (live first; how a program connects; Import); a capture opens as a document (`prof:<captureId>` tab kind) - the toolbar (recording state, Pause view / Follow and Stop while live, Threads or Tasks, Show all, the tool windows, Ask AI), the frame time graph (a click zooms to a frame, a drag to a run), the canvas timeline (`ui/timeline.js`, `timelinedraw.js`; theme tokens read once a paint), the zone details beside it, and the bottom tool windows Statistics (CSV), Find Zone, Messages, Memory and Flame Graph over the selection, else the view (Messages and Memory over the whole capture). A live capture follows its newest data until the timeline is moved. On a phone the details are a sheet under the timeline and a tool window takes the whole view. The status bar says "Capturing" while a program of the workspace records. Ask AI starts a chat with the question and an excerpt naming the capture, so the AI can use its tools on it.

**The AI** (`docs/13` T-PRF): `profile_list`, `profile_summary`, `profile_top_zones`, `profile_frame`, `profile_find_zone`, `profile_flame`, `profile_messages`, `profile_leaks`, `profile_compare` and `profile_capture` are C4 functions that answer through the plugin's `profiler.query` server binding as compact text (times with units, aligned tables, an indented call tree); `profile_capture` runs the startup project's Profile task (`IED::StartTask`), waits for its program to connect, records for the asked seconds, stops the capture and cancels the run. The `profiler` skill says how to instrument a program in each language.

**The Profile task.** A task in the `profile` slot starts the program with `IED_PROFILER=${profiler.connect}`; the `nuke` preset has one (`plugins/nuke`). Debug > Start Profiling (Alt+F2) runs it and opens the capture when the program connects.

**Clients.** Bedrock `Profile::` (`C4/Bedrock/Profile/`, opt-in `include "profile.ch"`): zones as scope guards over a static `Site` (the `PROFILE_ZONE("Name")` macro records the file and line) or a name looked up at run time, frames, plots, messages, thread names at any time, tasks, pools' allocations, and collections from the runtime's collection callback (the start is the end less the reported pause; the runtime has no start hook yet) with the GC heap plotted ten times a second. Events are stamped with `rdtsc`, its frequency measured against the system counter at start (`Hello` flags it as estimated; `Calibration` pairs every second correct it on the server). Each thread writes encoded events into its own buffer under an uncontended lock; a flusher thread sends them every 10 ms. One relaxed load per call when no session runs; `-D PROFILE=0` compiles the macros away. A send that fails ends the session. Tested by `C4/Bedrock/Tests/profile_tests.cf` (a session to a file read back event by event).

**C and C++** (`C4/Frost/Projects/Profiling/src/Profiling/Itp.h`, `Itp.cpp`): the same client in two files that need only the C++ standard library and the system's sockets (Windows or POSIX), so any C or C++ program can copy them. `ITP_ZONE("Name")` (a C++ scope guard; `ITP_ZONE_BEGIN`/`ITP_ZONE_END` in C), named zones with text and a value, `itp_frame`, `itp_plot`, `itp_message`, `itp_thread_name`, tasks, pools; `itp_start`/`itp_stop` for tools, else the first call starts from `IED_PROFILER` and the session ends at exit. `itp_alloc`/`itp_free` may be called from a replaced `operator new`: the client's own allocations are not reported. `ITP_ENABLE=0` compiles the macros away. Frost's `Performance.h` macros, its logger (`itp_message`) and its memory tracker report through it; it replaced the Tracy client (the submodule and the Tracy GUI setup package are gone). Tested by `tools/pylib/tests/test_itp_client.py`: built with the vendored clang, run to a file and to a listener, read back with `itp.py`.

**Importers** (`code/importers.cf`): the Chrome trace event format (`X`, `B`/`E`, instants, counters, async `b`/`e` as tasks, thread and process names; Rust's `tracing-chrome`, Chrome, Perfetto JSON, Nuke's build trace) and pprof (gzip and protobuf decoded in C4; Go's `runtime/pprof`, Rust's `pprof-rs`, gperftools): samples with their stacks and symbols, flame graph and statistics from them.

**Checks**: 27 unit tests beside the code (`*_tests.cf`), `tools/server-checks/tests/test_profiler.py` (9: live capture to file, queries, restart, refused connections, warnings, imports, the AI's answers, `${profiler.connect}`, `profile_capture`), the browser unit tests `frontend/tests/profiler.test.js`, the e2e scenario `profiler_live_capture` (desktop and phone), `tools/pylib/tests/test_itp_client.py` (the C/C++ client), and `tools/pylib/itp.py`, the protocol's reference codec in Python.

## Open items

- LZ4 in the client means a C dependency in Bedrock; v1 can ship uncompressed (localhost bandwidth is plentiful) and add compression with the first remote-capture use case.
- Not built yet: locks in the UI (the events are kept), GPU zones from a client (`Profile::Gpu`, D3D12 timestamp queries; the server and the timeline take them), annotations in the UI, symbolizing raw addresses from PDBs, UDP announce, captures larger than memory (indexes on disk), the debugger's Diagnostic Tools graphs (F-PRF-63).
- Sampling and context switches (ETW) are P2 and outside ITP v1 (the server collects them directly via an ETW helper and merges by timestamp).
