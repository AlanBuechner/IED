# 22 - GPU debugger

## Purpose

A graphics debugger as its own sidebar activity (**GPU**, plugin `ied.gpu`): capture frames from D3D12 and Vulkan programs, replay them on the server, inspect every event, debug shaders and pixels, and measure where GPU time goes. The owner works mostly in PIX, so **the UI follows PIX's layout and styling** (owner decision, 2026-10-19: RenderDoc's look is not wanted). RenderDoc contributes inspection depth and openness - as a backend and as capabilities placed inside PIX's layout - and Nsight contributes performance analysis. On top come what none of them have: AI that can drive the whole debugger, capture diffs across builds, one timeline with the CPU profiler, and triage from any device.

Feature list: `docs/features/17-gpu-debugger.md`. Mocks: `frontend/mocks/07-gpu-frame.html`, `08-gpu-shader-debug.html`, `09-gpu-performance.html`.

Generic by rule (AGENTS.md rule 1): the plugin knows D3D12, Vulkan and HLSL/SPIR-V, never Claymore. Engine meaning (pass names, material and entity names, what a bindless index points at) arrives inside the capture as debug markers, object names and an optional metadata chunk that any engine can write.

## What we take from each tool

| Area | PIX | RenderDoc | Nsight Graphics | IED |
|---|---|---|---|---|
| Event list | marker tree with per-event GPU timing columns | marker tree, bookmarks, search, API call list per event | frame debugger event list | PIX's events grid (API calls as rows under markers, GPU time and statistics columns) plus bookmarks and a filter language (`kind:draw ps:DefaultLit rt:HDR`) |
| State | Pipeline view: state tree by stage beside the selected resource | pipeline flowchart, bindings with contents, root signature | API inspector | PIX's state tree (no flowchart); **bindless indices resolved to the resource and its name**; constant buffers decoded in the tree |
| Textures | resource viewer with channels, mips, range, pixel status line | overlays (wireframe, depth/stencil test, overdraw, clipping, triangle size), pixel context, thumbnails | resource viewer | PIX's resource viewer and status line; RenderDoc's overlays as entries of a *Visualize* menu (no thumbnail strip, no floating pixel box); **display transforms run in the browser (WebGL2), instant even on a phone** |
| Geometry | mesh view | mesh viewer: VS in/out, mesh/task output, 3D preview, table | geometry view | RenderDoc mesh viewer |
| Pixel history | yes | yes, with test-failure reasons per event | yes | RenderDoc history + "why" summary in plain words |
| Shader debugging | HLSL source stepping | DXBC/DXIL/SPIR-V, source-level with debug info | yes | source-level stepping in an IED editor surface, watch/locals per lane; **later: time-travel debugging by instrumented replay** (Flashbang) |
| Edit and replay | shader edit and continue | shader edit and replace | shader edit | edit in the IED editor (with the shader language plugin's LSP), replay, **show a diff of the output** |
| Timing | timing captures, per-event GPU time, queue timeline | per-event duration | GPU Trace: queues, unit throughput, warp occupancy | per-event and per-pass time, queue timeline with bubbles and fences |
| Counters | vendor counters | via vendor plugins (limited) | full hardware metrics, "speed of light" per unit, shader profiler stall reasons | vendor providers (AMD GPA, NVIDIA Perf SDK, Intel) in the replay process; SOL-style bars and a limiter verdict |
| Analysis | warnings / automated analysis | none built in | GPU Trace analysis | **insights**: debug-layer and GPU-based-validation messages tied to events, redundant barriers, overdraw, tiny draws, NaN/Inf writes, unused outputs - each with "Ask AI" |
| New APIs | work graphs, DXR | DXR, mesh shaders, no work graphs in its release notes | DXR inspector | through the backend; work graphs and bindless-everything are why Flashbang exists |
| Openness | closed `.wpix`, programmatic capture API | MIT, C++ replay API, Python bindings | closed | IED owns the protocol and data model; backends are swappable |

## Architecture

```
browser (ied.gpu UI module)                         any device; WebGL2 for texture display and mesh preview
   |  protocol v1: topics gpu.*, requests, blobs over HTTP (docs/10)
ied-server  --  plugin ied.gpu (builtin, C4)
   |   capture library + metadata index, launch/attach through tasks, sessions,
   |   insights engine, compare, AI tools (T-GPU-*), bindings for profiler/editor/chat
   |  ied-gpu/1: JSON-RPC over stdio + shared-memory blobs, one process per open capture
replay backend (native process, crash-isolated, restartable)
   +-- rdc        C++ adapter over RenderDoc's replay API (renderdoc_replay.h)        phase 1
   +-- flashbang  IED's own replayer for .flash captures                              phase 2
target program
   +-- rdc:       renderdoc.dll injected at launch; in-app API for triggers and comments
   +-- flashbang: recorder compiled into C4 programs through Bedrock Gpu::  (no injection)
```

Why a separate native process: GPU replay crashes drivers' user-mode code often enough that it must never take the server down; native APIs reach C4 only through hand-written C shims today (`docs/17`), and a process boundary sidesteps CBind entirely; one process per capture lets two captures replay side by side for compare.

The server never ships raw GPU objects to the browser. Textures go as decoded tiles (RGBA16F/32F, requested mip/slice/region, downsampled to the viewport); the browser applies range, channels, gamma, NaN/Inf/negative highlighting and zoom in a WebGL2 shader. Overlays that need replay (wireframe, depth/stencil test, overdraw, triangle size, highlight draw) are rendered by the backend into an overlay tile. Pixel picks are typed values read by the backend (exact format, not the display value). Mesh data goes as vertex arrays for a WebGL2 preview.

## Backends: RenderDoc, our own, or both

The question the owner asked: use RenderDoc as the backend, or build our own with RenderDoc as the reference ("more control, no Python runtime").

**No Python is needed either way.** RenderDoc's replay API is C++ (`renderdoc/api/replay/renderdoc_replay.h`); its own Qt UI uses it directly, and the Python module is a wrapper over the same interfaces. An adapter process links `renderdoc.dll` and speaks `ied-gpu/1`; nothing else of RenderDoc ships.

| Option | Gets us | Costs | Limits we would hit |
|---|---|---|---|
| **A. RenderDoc backend** (adapter process) | A complete D3D11/12 + Vulkan debugger in weeks: capture, replay, state, textures, meshes, pixel history, DXIL/SPIR-V shader debugging. MIT license. | Pinning and adapting to its API across versions; its replay model decides what is possible. | Shaders with unbounded cbuffer arrays are marked not debuggable (release notes v1.45) - Icepick's `DefaultLit.hlsl` declares `ConstantBuffer<Material> u_Materials[]`, so Claymore's main lit shader is affected. Work graphs do not appear in its release notes (Icepick has `WorkGraphShaderPass`). Its notes warn of an AMD driver bug breaking multi-thread shader debugging. No engine semantics beyond names and markers. |
| **B. Own backend from scratch** (Flashbang), RenderDoc as reference | Full control: bindless, work graphs, engine metadata, multi-frame and rolling captures, our own capture format, shader debugging designed for our needs. | Capture layer, serializer, replayer, format decoders, pixel history and a shader debugger: many months before it matches A. | Everything is ours to maintain, per API and driver quirk. |
| **C. PIX as backend** | - | - | Not possible: `.wpix` is closed and there is no replay API. PIX stays a hand-off: programmatic capture (`WinPixGpuCapturer.dll`, `PIXGpuCaptureNextFrames`) and "Open in PIX". |
| **D. Nsight as backend** | - | - | Closed. Hand-off only ("Open in Nsight Graphics"). |

**Recommendation: A, then B, behind one protocol.** The UI, data model, protocol, insights and AI layer are IED's from day one - that is where "a strong debugger" lives and none of it depends on the backend. RenderDoc gives a complete debugger early. Flashbang is then built for C4 programs exactly where control pays off (unbounded/bindless resources, work graphs, engine semantics, rolling and multi-frame capture, time-travel shader debugging), reading RenderDoc's MIT source as the reference. If Flashbang covers the owner's needs, RenderDoc becomes optional. Owner's call: `docs/99` O11.

## Flashbang - IED's own capture and replay (phase 2)

Working name in the family's explosion theme (a flashbang lights up the whole frame for an instant); owner to confirm (`docs/99` O11).

- **Capture inside C4 programs, no injection.** Bedrock gets a `Gpu::` module (`docs/17`) that wraps the D3D12 calls a program makes; Claymore's RHI already routes every call through its `CommandList` abstraction, so recording is a layer under it rather than a hook. Programs opt in with a define (compiled out in `Dist`, like `Profile::`).
- **Engine semantics for free.** The recorder takes marker scopes from `Profile::Gpu::Zone` (so capture events and profiler GPU zones share names), object names, and an optional `Gpu::Annotate` table: bindless slot -> asset path, material -> source file, draw -> entity. Any engine can write the same chunk; IED only displays it.
- **Format `.flash`:** chunked like ITP (`docs/16`): interned strings, resource table, per-command-list API streams, initial contents of referenced resources (zstd), shader blobs + PDBs, metadata chunk. Rolling mode keeps the last N frames in a ring and writes them out on a trigger ("capture on hitch": the profiler sees a spike and keeps the frames around it).
- **Replayer:** `flashbang-replay.exe` implements `ied-gpu/1`. Native C++ first; C4 later is possible because it is an executable, not a DLL.
- **Shader debugging by instrumented replay (time travel).** Instead of emulating DXIL, recompile the shader from source with DXC plus injected logging for one selected thread or quad, replay the event on the real GPU, and record every variable write into a buffer. The UI then steps forward *and backward* through the recorded trace. Real hardware means correct wave ops, derivatives and bindless reads; emulation (RenderDoc's approach) stays the reference for shaders without source.
- **Non-C4 programs** keep using RenderDoc (or a later C++ layer DLL); Flashbang does not need to cover the world.

## Views

PIX's structure, restyled with IED's tokens: a capture opens as a **document tab** (several captures side by side for compare); each capture document has **view tabs** - Overview, Pipeline, Debug, Performance, Compare; a **timing timeline** runs across the top (marker rows, one row per queue, counter plots); the **events grid** is on the left in every view; panes are dense grids with VS-style docked tool windows at the bottom (Warnings, Pixel History, Resource History, API Calls, Messages). The activity's start page is the capture library.

| View | Contents |
|---|---|
| Capture library | captures per workspace with thumbnail, program, config, git commit, frame, GPU, size, backend; comments; pin; compare; open in PIX / Nsight / RenderDoc UI |
| Events grid | markers as tree rows, API calls as rows (`DrawIndexedInstanced(36, 1, 0, 0, 37)` with the object name), columns EID, GPU ms with an in-cell bar, primitives, PS invocations; warning badges; bookmarks; filter language |
| Timing timeline | marker rows (frame, passes, draws), one row per queue (Direct, Compute, Copy, Video) with idle bubbles, counter plots (GPU busy, occupancy, bandwidth), range selection |
| Pipeline view | state tree by stage (Input Assembler ... Output Merger, Root Signature, Descriptor Heaps) with every binding; bindless indices resolved with names; constant buffers decoded with the shader's reflection; selecting an item shows it in the resource viewer |
| Resource viewer | the selected texture or buffer: mip/slice/sample, channels, range, *Visualize* menu (NaN/Inf/negative, depth/stencil test, overdraw, wireframe, triangle size, highlight draw), zoom; a status line with the pixel, typed value, depth and last write; Pixel History / Debug Pixel / Resource History actions |
| Mesh Viewer | VS input/output, mesh/task output, table + 3D preview with camera from the capture |
| Resources | all resources with usage, format, size, lifetime (first/last EID), memory heap, aliasing; per-resource history of writes and reads |
| API Calls | the calls inside the selected event, parameters, callstack if captured |
| Pixel History | every event that touched a pixel: before/after values, shader output, which test failed (depth, stencil, scissor, culling, blend), plain-words summary |
| Shader Debugger | source in an editor surface (LSP from the shader-language plugin), breakpoints, step in/over/out (and back, on Flashbang), watch, locals, per-lane view, disassembly toggle |
| Messages | debug layer and GPU-based validation output tied to EIDs |
| Performance | passes grid with A/B columns (A ms, B ms, delta) and statistics, counters grid for the selected range with throughput bars and a limiter verdict, Analysis findings, the timeline with counter plots |
| Warnings / Analysis | one grid for debug-layer messages, GPU-based validation and IED's analysis (NaN writes, regressions, culling, barriers, overdraw, queue bubbles), each row linked to its EID with Ask AI |

Phone (one pane at a time, `docs/15`): capture library, event list with timings, texture preview with picks, pixel history, insights and AI answers. Shader stepping and the mesh viewer are desktop-only.

## Capture flow

- **Launch:** a `Capture` task preset (toolbar button next to *Profile*) builds, then launches the program under the chosen backend (RenderDoc injection, or the Flashbang define + env var). The next frame is captured by the in-app hotkey, by IED's *Capture next frame* command (works from the phone), or by code.
- **From code:** Bedrock `Gpu::Capture::NextFrames(n)`, `Begin/End`, `SetComment` - implemented over RenderDoc's in-app API (`renderdoc_app.h`), the PIX programmatic API, or the Flashbang recorder, whichever is active.
- **From the profiler:** "capture on hitch" and "capture this frame" on a GPU zone. A capture taken during a profiling session links to the ITP trace; events and GPU zones cross-navigate.
- **Storage:** captures live under the per-machine workspace state (`.ied/local/captures/`, gitignored) with a sidecar JSON of metadata; the library indexes them. Pin a capture to keep it past the retention limit.

## AI

GPU tools (`docs/13`, group `gpu`) let the agent work the debugger the way the owner does: list and search events, read state, read pixel values and histories, run shader debugging and get the trace as data, scan a render target for NaN/Inf, bisect which event first wrote a bad value, fetch timing and counters, compare two captures, and look at a texture (vision models only; privacy rules apply). Typical asks: "why is this pixel black?", "which pass got slower since yesterday's build?", "why does the shadow pass take 3 ms?". Answers cite EIDs, which the UI turns into links.

## Integrations

- **Profiler:** shared marker names; GPU zone -> capture event and back; capture on hitch.
- **Editor:** shaders open at the right line; breakpoints set in the editor apply to the shader debugger; Edit and Replay writes through the document store, so the edit can be saved as a real change.
- **Git:** capture metadata records the commit; compare two captures across commits; "bisect this visual regression" runs captures across a commit range (with the orchestration queue).
- **Chat:** attach an event, pixel, texture or capture (`@gpu:...`); the agent's GPU tool cards render thumbnails and value tables.
- **Tests:** a capture plus expected values can be a test (golden image or pixel assertions) through the test provider binding.

## Phases

| Phase | Ships | Depends on |
|---|---|---|
| G1 | rdc backend; library; launch capture; Event Browser, Pipeline State, Texture Viewer, Resources, API Calls, Messages, Pixel History, Shader Debugger (RenderDoc's), Mesh Viewer; per-event timing; read-only AI tools | process spawn + blobs (M1), tasks (M3), docking (M2) |
| G2 | Performance view with counters providers; compare and insights; Edit and Replay; phone triage; capture on hitch with the profiler | G1, profiler (M9) |
| G3 | Flashbang: Bedrock `Gpu::` recorder, `.flash`, replayer, time-travel shader debugging, work graphs, rolling captures | G2, Bedrock `Gpu::` (C4 repo) |

Roadmap placement: `docs/18` M12 (G1-G2) and M13 (G3).

## Risks

- Replay is driver-sensitive: replay on the capturing GPU by default, show the capture's GPU/driver beside the replay's, and keep the backend restartable.
- Captures are large (GBs): stream from disk, decode tiles lazily, retention limits.
- RenderDoc's replay API is internal-facing and changes between versions: pin a version, cover the adapter with tests against stored captures.
- Vendor counter SDKs differ in licensing and coverage: providers are optional plugins, D3D12 timestamps and pipeline statistics always work.
- The server needs the GPU: the replay runs where the IED server runs (the owner's desktop); the phone only views.
