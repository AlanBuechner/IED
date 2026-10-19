# Features: profiler (PRF)

Owner: `plugins/profiler` (UI, analysis) on `src/profiler` (capture server, trace storage). Instrumentation side: Bedrock (C4 repo) and any other client that speaks the trace protocol. Design and wire format: `docs/16`.

Tracy is the **feature reference only** (never linked). Where Tracy has a known limit, the "Beyond Tracy" column says what IED does differently.

## Capture and sessions

| ID | Feature | Pri | Stories | Beyond Tracy |
|---|---|---|---|---|
| F-PRF-1 | Capture server: instrumented apps connect to IED (TCP localhost; port announced via env var `IED_PROFILER` set by the *Profile* task, plus UDP discovery for apps started elsewhere) | P1 | US-PRF-1 | Apps connect *to* IED, so launch-under-profiler needs no discovery |
| F-PRF-2 | Streaming to disk: events are written to a chunked trace file as they arrive, with an in-memory index; captures larger than RAM work | P1 | US-PRF-1 | Tracy keeps everything in RAM until save |
| F-PRF-3 | Live view while capturing (timeline scrolls with "now"), pause view without stopping capture | P1 | US-PRF-1 | - |
| F-PRF-4 | Robust ingestion: an instrumentation error (unbalanced zone, free without alloc) is recorded as a warning event and the capture continues | P1 | US-PRF-1 | Tracy drops the connection |
| F-PRF-5 | *Profile* standard task (build -> launch with env -> attach capture); auto-start capture when a run task launches if the project opts in (`build.status` binding) | P1 | US-PRF-1 | - |
| F-PRF-6 | Capture library: list of saved traces per workspace with metadata (app, config, git commit, duration, size), open, delete, compare | P1 | US-PRF-1 | - |
| F-PRF-7 | Import: Chrome trace JSON (Nuke `build-trace.json`, Claymore `claymore profile` output), and Tracy `.tracy` files (P2) | P2 | US-PRF-7 | - |

## Timeline

| ID | Feature | Pri | Stories | Beyond Tracy |
|---|---|---|---|---|
| F-PRF-10 | Timeline with frame bands (multiple named frame sets), per-thread tracks with zone stacks, zoom/pan (wheel, drag, keyboard), range selection, measurements | P1 | US-PRF-1 | - |
| F-PRF-11 | Frame time graph (top strip) with target-FPS line and spike highlighting; click a frame to zoom | P1 | US-PRF-1 | - |
| F-PRF-12 | **Thread rename history**: thread name is a time series; each track shows its current name, and segments after a rename get a subtle background tint with the name drawn at the boundary | P1 | US-PRF-3 | Tracy reads a thread name once |
| F-PRF-13 | **By-task view mode**: tracks are rebuilt by task identity instead of OS thread; a task identity comes from explicit `TaskBegin/TaskEnd` events or, when absent, from thread-name segments (a renamed worker's segments move to the lane of the task name); lane shows which OS thread ran each slice (thin color strip + tooltip) | P1 | US-PRF-3 | Tracy has fibers (one track per pointer) but no task view |
| F-PRF-14 | Lane grouping/filters: group threads by group hint, hide/pin/collapse lanes, depth limit, search lanes | P1 | US-PRF-1 | - |
| F-PRF-15 | Zone details: name, source location (click -> editor via `editor.openFile`), duration (self/inclusive), text/value annotations, children, parent chain, call stack if captured | P1 | US-PRF-1 | - |
| F-PRF-16 | Messages/log lane + Messages window (filter by severity, thread, text; jump to time) | P1 | US-PRF-1 | - |
| F-PRF-17 | Plots: user plots (number/memory/percentage), automatic memory and GC plots, CPU usage plot | P1 | US-PRF-1 | - |
| F-PRF-18 | Locks: lock lanes (uncontended / contended / blocked), lock info window, contention statistics | P2 | US-PRF-1 | - |
| F-PRF-19 | User annotations (time ranges with notes) stored inside the trace file | P2 | - | Tracy uses a sidecar file |

## Analysis

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-PRF-20 | Statistics window: per zone name - count, total, self, mean, median, p95, max; per-thread or per-task breakdown; sortable; CSV export | P1 | US-PRF-1 |
| F-PRF-21 | Find zone: histogram of a zone's durations, select a histogram range to list instances, jump to them | P1 | US-PRF-1 |
| F-PRF-22 | Flame graph / icicle (aggregate over selection or whole capture) | P1 | US-PRF-1 |
| F-PRF-23 | Compare two captures: zone statistics diff, frame time distributions overlaid | P2 | US-PRF-1 |
| F-PRF-24 | Range-limited analysis: every window can be limited to the timeline selection | P1 | US-PRF-1 |

## Memory

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-PRF-30 | Allocation events (alloc/free with size, pool name, optional call stack) -> memory over time per pool | P1 | US-PRF-2 |
| F-PRF-31 | Leak list: allocations live at capture end (or at a chosen time), grouped by call stack (bottom-up and top-down trees) | P1 | US-PRF-2 |
| F-PRF-32 | Allocation search by address; allocation lifetime view; "allocations made in this zone" | P2 | US-PRF-2 |
| F-PRF-33 | GC events: collection begin/end (pause shown on all threads), generation, bytes before/after, pinned/roots counts, plus GC stats plots (C4 `GC::GetStats`) | P1 | US-PRF-2 |
| F-PRF-34 | Memory map view (address space occupancy) | P2 | US-PRF-2 |

## GPU

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-PRF-40 | GPU contexts as lanes (per queue), GPU zones with CPU-side submit markers and connector lines | P1 | US-PRF-4 |
| F-PRF-41 | Calibration: periodic CPU/GPU timestamp pairs (D3D12 `GetClockCalibration`) with drift correction | P1 | US-PRF-4 |
| F-PRF-42 | D3D12 first (Claymore); Vulkan later through the same events | P1 (D3D12), P2 (Vulkan) | US-PRF-4 |
| F-PRF-43 | Per-pass GPU statistics and frame breakdown table | P1 | US-PRF-4 |
| F-PRF-44 | Link to external frame captures (PIX) when the app reports one was taken | P2 | - |

## Sampling and system (later)

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-PRF-50 | Context switches and CPU core lanes via ETW (requires elevation; optional helper) | P2 | - |
| F-PRF-51 | Call stack sampling via ETW, with symbol resolution from PDBs (C4 emits CodeView) | P2 | - |
| F-PRF-52 | Source view with per-line sample heat | P2 | - |

## AI and integration

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-PRF-60 | AI tools (T-PRF-*): start/stop capture, summarize capture, top zones (by self time, in range), frame breakdown, compare captures, leak summary, GPU pass summary | P1 | US-PRF-6 |
| F-PRF-61 | "Ask AI about this selection" (zone, frame range, leak group) -> chat with a structured excerpt attached | P1 | US-PRF-6 |
| F-PRF-62 | Trace files open in the profiler as a custom editor (F-EDT-97) | P1 | US-EDT-11 |
| F-PRF-63 | Live CPU/memory graphs for the debugger's Diagnostic Tools pane (F-DBG-52) | P2 | US-DBG-5 |
