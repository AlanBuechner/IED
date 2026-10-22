# 22 - GPU debugger

## Purpose

A graphics debugger as its own sidebar activity (**GPU**, plugin `ied.gpu`): capture frames from D3D12 and Vulkan programs, replay them on the server, inspect every event, debug shaders and pixels, and measure where GPU time goes. The owner works mostly in PIX, so **the UI follows PIX's layout and styling** (owner decision, 2026-10-19: RenderDoc's look is not wanted). The capture and replay backend is IED's own, **Flashbang** (owner decision, 2026-10-21): RenderDoc is the reference for how a hard problem is solved (its source is MIT) and for inspection depth, Nsight for performance analysis, but neither is a dependency. On top come what none of them have: AI that can drive the whole debugger, capture diffs across builds, one timeline with the CPU profiler, and triage from any device.

Feature list: `docs/features/17-gpu-debugger.md`. Mocks: `frontend/mocks/07-gpu-frame.html`, `08-gpu-shader-debug.html`, `09-gpu-performance.html`. Code: `plugins/gpu/` (`README.md` there maps the folders).

Generic by rule (AGENTS.md rule 1): the plugin knows D3D12, Vulkan and HLSL/SPIR-V, never a specific engine. Engine meaning (pass names, material and entity names, what a bindless index points at) arrives inside the capture as debug markers, object names and an optional metadata chunk that any engine can write.

## What we take from each tool

| Area | PIX | RenderDoc | Nsight Graphics | IED |
|---|---|---|---|---|
| Event list | marker tree with per-event GPU timing columns | marker tree, bookmarks, search, API call list per event | frame debugger event list | PIX's events grid (API calls as rows under markers, GPU time and statistics columns) plus bookmarks and a filter language (`kind:draw ps:DefaultLit rt:HDR`) |
| State | Pipeline view: state tree by stage beside the selected resource | pipeline flowchart, bindings with contents, root signature | API inspector | PIX's state tree (no flowchart); **bindless indices resolved to the resource and its name**; constant buffers decoded in the tree |
| Textures | resource viewer with channels, mips, range, pixel status line | overlays (wireframe, depth/stencil test, overdraw, clipping, triangle size), pixel context, thumbnails | resource viewer | PIX's resource viewer and status line; RenderDoc's overlays as entries of a *Visualize* menu (no thumbnail strip, no floating pixel box); **display transforms run in the browser (WebGL2), instant even on a phone** |
| Geometry | mesh view | mesh viewer: VS in/out, mesh/task output, 3D preview, table | geometry view | RenderDoc's mesh viewer content in a PIX pane |
| Pixel history | yes | yes, with test-failure reasons per event | yes | RenderDoc's history + "why" summary in plain words |
| Shader debugging | HLSL source stepping | DXBC/DXIL/SPIR-V, source-level with debug info | yes | source-level stepping in an IED editor surface, watch/locals per lane; **time-travel debugging by instrumented replay** |
| Edit and replay | shader edit and continue | shader edit and replace | shader edit | edit in the IED editor (with the shader language plugin's LSP), replay, **show a diff of the output** |
| Timing | timing captures, per-event GPU time, queue timeline | per-event duration | GPU Trace: queues, unit throughput, warp occupancy | per-event and per-pass time, queue timeline with bubbles and fences |
| Counters | vendor counters | via vendor plugins (limited) | full hardware metrics, "speed of light" per unit, shader profiler stall reasons | vendor providers (AMD GPA, NVIDIA Perf SDK, Intel) in the replay process; SOL-style bars and a limiter verdict |
| Analysis | warnings / automated analysis | none built in | GPU Trace analysis | **insights**: debug-layer messages tied to events, redundant barriers, overdraw, tiny draws, NaN/Inf writes, unused outputs - each with "Ask AI" |
| New APIs | work graphs, DXR | DXR, mesh shaders, no work graphs | DXR inspector | ours to add as needed: bindless and work graphs are why the backend is our own |
| Openness | closed `.wpix`, programmatic capture API | MIT, C++ replay API, Python bindings | closed | IED owns the capture format, the replayer, the protocol and the data model |

## Decision: our own backend (2026-10-21)

The owner chose our own capture and replay over RenderDoc as a backend ("as few dependencies as possible ... build your own that we can have control over"), for **both D3D12 and Vulkan**, with a C4 library so projects can use it (`docs/99` O11). Earlier this doc recommended RenderDoc first and our own later; that plan is replaced:

| Option | Why not / why |
|---|---|
| RenderDoc as backend | A complete debugger early, but its replay model decides what is possible: shaders with unbounded descriptor arrays are not debuggable, no work graphs, no engine semantics, a pinned external DLL and API to follow. Stays a *reference* (its MIT source) and, later, a hand-off ("Open in RenderDoc" when installed). |
| PIX / Nsight as backend | Not possible: closed formats, no replay API. Hand-offs only. |
| **Flashbang (ours)** | Full control over capture, format and replay; bindless, work graphs, engine metadata and time-travel shader debugging are ours to add. Costs: every API path and driver quirk is ours to maintain, so coverage grows feature by feature with tests against sample programs. |

Dependencies, all already part of the setup: the Windows SDK's `d3d12.h` / `dxgi` headers (and its `dxcompiler.dll` for DXIL reflection and disassembly, used when present), Khronos' Vulkan headers (pinned in `tools/setup/deps.toml`), and the toolchain's clang and lld. Nothing of RenderDoc ships or is linked.

## Architecture

```
browser (plugins/gpu/ui)                         PIX-style views; WebGL2 texture display and mesh preview
   |  protocol v1 (docs/10): gpu.* methods, gpu:<wsId> and gpurs:<sessionId> topics, blobs
ied-server  --  built-in plugin ied.gpu (plugins/gpu/code, C4)
   |   capture library, launch for capture, target control, replay sessions, AI tools
   +-- \\.\pipe\flashbang-<pid>   JSON lines   --> flashbang.dll inside the running program
   +-- stdio  ied-gpu/1           JSON-RPC     --> flashbang-replay.exe, one process per open capture
target program
   +-- flashbang.dll, loaded by the program itself:
       D3D12: the program hands its device and DXGI factory to the capture library (Bedrock Gpu::Capture,
              or flashbang_app.h for C/C++), which returns instrumented ones
       Vulkan: the loader's own layer mechanism, VK_LAYER_IED_flashbang, enabled by IED's launch environment
```

The native parts are C++ (`plugins/gpu/native/`, built by `nuke build flashbang` with the toolchain's clang): a capture layer has to be a DLL, which C4 does not produce, and the capture DLL and the replayer share one serialization of every API structure. The replayer is a separate process because replay drives driver code that can crash: a crash never takes the server down, the session restarts and the UI keeps its place, and two captures replay side by side for compare.

The server never ships GPU objects to the browser. Textures go as decoded float data (RGBA32F of the requested mip/slice, downsampled to the view); the browser applies range, channels, gamma, NaN/Inf/negative highlighting and zoom in a WebGL2 shader. Pixel picks are typed values read by the replayer (exact format, not the display value). Mesh data goes as vertex arrays for a WebGL2 preview.

## Capture (flashbang.dll)

**D3D12** (`native/capture/d3d12/`) is **opt-in by the program**: right after creating its device and its DXGI factory, the program passes them to the capture library (`Gpu::Capture::WrapD3D12Device` / `WrapDxgiFactory` in C4, `flashbang::WrapDevice` / `WrapFactory` from `native/include/flashbang_app.h` in C/C++), which gives back instrumented objects; without the library loaded the same calls return the program's own objects, so the calls can stay in every build but `Dist`. Only the objects whose calls a capture must see are proxies - the device, command queues, command lists, the DXGI factory and swap chains - each forwarding every method of every interface version the runtime supports (the forwarding is generated from the SDK headers, `native/capture/d3d12/gen_wrappers.py`). Resources, heaps, pipeline states and every other object stay the runtime's own, so structures that carry them need no translation; only the proxies' own types are unwrapped where they are passed (`ExecuteCommandLists`, `ExecuteBundle`, a swap chain's queue). The program never sees a call it did not make: nothing is patched or redirected outside the objects it handed over.

- Object tracking: every creation (heaps, committed/placed/reserved resources, descriptor heaps, root signatures, pipeline states, command signatures, query heaps, fences, queues) is kept as its creation record (with shader bytecode and root signature blobs). Lifetime comes from a private-data interface the DLL attaches to each object (`SetPrivateDataInterface`): it is released when the object dies. Names are read at capture time from `WKPDID_D3DDebugObjectNameW`, where `SetName` keeps them.
- Descriptors: `Create*View`, `CreateSampler` and `CopyDescriptors*` write a shadow of every descriptor heap (CPU handle -> heap and index), so the capture knows what every slot held, bindless heaps included.
- GPU addresses: buffers' virtual address ranges are kept sorted; every address the program passes (root descriptors, vertex/index buffer views, `WriteBufferImmediate`) is stored as (resource, offset) and rebuilt on replay. GPU descriptor handles are stored as (heap, index). Addresses written *inside* buffers (indirect arguments with addresses, ray tracing instances) are not rewritten; the capture warns when it sees them.
- Command lists are recorded always (each call serialized into the list's buffer, cleared by `Reset`), because a list recorded before the captured frame may run in it. Barriers in submitted lists move the tracked state of each resource, which the snapshot needs.
- A capture: the trigger arms it; at the next `Present` the DLL waits for every queue to idle, snapshots the **initial contents** of every live resource (default-heap resources by GPU copies into readback buffers, upload-heap resources by reading their mapped memory, which is then shadowed so the frame's CPU writes are recorded as diffs before each `ExecuteCommandLists`), records every submission until the next `Present`, waits for idle again and writes the file.

**Vulkan** (`native/capture/vulkan/`): the same DLL is a regular Vulkan layer (`VK_LAYER_IED_flashbang`, its manifest written beside the DLL), which the Vulkan loader itself loads when IED's launch environment names it (`VK_ADD_LAYER_PATH`, `VK_INSTANCE_LAYERS`), with the loader's dispatch tables - the mechanism the loader documents for tools, so a Vulkan program needs no code change. It tracks the same things the Vulkan way: memory allocations and bindings, buffers, images and views, samplers, descriptor set layouts, pools and sets (writes and copies), pipeline layouts, shader modules, pipelines, render passes, framebuffers; image layouts from barriers and render pass final layouts; command buffers recorded always (a secondary command buffer is inlined where `vkCmdExecuteCommands` runs it); descriptor sets shadowed from their writes and copies; the frame's writes to mapped memory as diffs before each submit. Buffers and images get `TRANSFER_SRC` / `TRANSFER_DST` usage added at creation (swap chain images too, when the surface allows it), so the snapshot can copy any of them: host-visible memory is read in place, the rest is copied into readback buffers at the frame start. The thumbnail is copied from the presented image before the present, which waits for the copy. Every Vulkan structure's serialization is generated from the registry (`native/common/gen_vulkan.py` reads `vk.xml`); a `pNext` structure the generator does not know is left out of the record and named in the capture's warnings.

**Triggers:** IED's *Capture Next Frame* (the control pipe), or the program itself through `Gpu::Capture::TriggerCapture(n)` (bound to whatever key or console command the program likes). A frame ends at `Present` / `vkQueuePresentKHR`; a program without a swap chain marks frames with `Gpu::Capture::StartFrame` / `EndFrame`.

**Control pipe** `\\.\pipe\flashbang-<pid>`, one JSON object per line. The server sends `{"cmd":"capture","frames":n}`, `{"cmd":"status"}`; the DLL answers `{"event":"hello","pid","program","apis","frame","captureDir"}` on connect, `{"event":"status",...}`, `{"event":"captured","path","frame","bytes"}`, `{"event":"error","message"}`. Only the user who runs the program can open it: the pipe's descriptor has that user's access entry alone (a pipe's default one also lets Everyone and Anonymous read), it is created as the first instance of its name so it never joins a pipe another process made first, and it refuses remote clients. The server checks the other way that the pipe it connects to is served by a process of its own user (Bedrock `Net::LocalStream`).

## The capture file (`.flash`)

`<program>_<yyyymmdd-hhmmss>_f<frame>.flash` in the capture folder (`FLASHBANG_CAPTURE_DIR`; IED passes `<workspace>/.ied/local/gpu-captures`), with a sidecar `.json` (program, API, frame, GPU and driver, size, time, comment) the library reads without opening the capture and a `.png` thumbnail of the presented image.

- Header: magic `FLASHCAP`, format version, API (1 = D3D12, 2 = Vulkan), flags.
- Then records `{u32 type, u32 flags, u64 size, payload}`, in order: metadata (JSON text), the device, object creations in creation order, descriptor contents, resource states at the frame start, initial contents (each subresource's rows packed), then the frame: submissions (queue, the command lists with their recorded calls), mapped-memory writes, fence signals and waits, the present.
- Every API structure has **one symmetric serialization function** (RenderDoc's idea): the same code writes it in the capture DLL, reads it in the replayer (object ids mapped to the replay's objects, addresses rebuilt) and describes it as JSON with field and enum names for the API Calls window. The version in the header rejects files of another format version instead of misreading them.

## Replay (`flashbang-replay.exe`, protocol `ied-gpu/1`)

One process per open capture, started by the server with the capture's path. Messages on stdio as LSP frames them, with an optional binary part: `Content-Length: N\r\n[Binary-Length: M\r\n]\r\n<N bytes JSON-RPC 2.0><M bytes>`. Binary parts carry texture and buffer data; the server turns them into blobs.

| Method | Result |
|---|---|
| `initialize {capture, options}` | `{api, frame, program, captureDevice, replayDevice, counts, warnings}` |
| `events {}` | the event tree: per event `eid`, `parent`, `kind` (marker, draw, dispatch, copy, clear, barrier, present, call), `name`, `queue`, `flags`; markers with their spans |
| `apiCalls {eid}` | the calls that belong to the event (the state set since the previous action, then the action) with their parameters as JSON |
| `state {eid}` | the pipeline state at the event as a tree by stage: shaders, root signature or pipeline layout, every binding resolved to its resource and name, vertex/index buffers, render targets, viewports, rasterizer, blend, depth-stencil; an item naming a resource has `resource`, a render target, depth target or storage resource its `role` (`rt`, `depth`, `uav`), a buffer binding its `offset`, suggested `layout` and, for a constant buffer, `elements: 1` |
| `resources {}` | every resource: id, name, kind, dimensions, format, size, heap/memory, first and last use |
| `texture {id, eid, mip, slice, region?, maxSize?}` | `{width, height, format, min, max}` + RGBA32F data (binary) of the subresource after the event |
| `pixel {id, eid, x, y, mip, slice}` | the texel's typed value (`raw`, `float`, `text`) |
| `buffer {id, eid, offset, size}` | bytes (binary) |
| `mesh {eid}` | the draw's vertex inputs: attributes with formats, indices, vertex data (binary) |
| `shader {eid, stage}` | entry, kind, reflection (signature, bindings, constant buffers), disassembly |
| `timing {repeat}` | per action GPU time (median of `repeat` replays with timestamp queries) and pipeline statistics |
| `pixelHistory {id, x, y, eid}` | every event before `eid` that wrote the pixel, with values before and after |
| `messages {}` | the debug layer's messages during replay, each with its event |
| `shutdown {}` | ends the process |

**Vulkan replay** (`native/replay/vulkan/`) creates its own instance and device on the GPU with the capture's vendor and device ID when there is one (else the first), with the captured device extensions the GPU has and the captured features, then the capture's objects in three passes (memory, buffers and images; their bindings; everything else). Swap chain images become plain images. SPIR-V is disassembled and reflected by Flashbang's own code (`vkspirv.cpp`), with no SPIRV-Tools dependency; the shader panel shows bindings by set and binding, uniform blocks and push constants with their members, and the stage's inputs and outputs by location.

Replaying to an event re-applies the initial contents and replays every submission before it, then the event's command list up to the event (closing an open render pass or query so the list stays valid), and waits for the GPU. Pipeline state, resource states and bindings at an event are simulated on the CPU from the records, so the Pipeline view needs no GPU work.

## Server and protocol (plugin `ied.gpu`)

| Method | Does |
|---|---|
| `gpu.launch {wsId, project?, program?, args?, cwd?}` | builds the project (its debug configuration's pre-launch task) and starts its program with the capture environment (`FLASHBANG_DLL`, `FLASHBANG_CAPTURE_DIR`, the Vulkan layer's variables); the target appears on `gpu:<wsId>` |
| `gpu.capture {wsId, targetId, frames?}` | asks the running target for the next frame(s) |
| `gpu.stop {wsId, targetId}` | ends a target IED launched |
| `gpu.delete {wsId, captureId}`, `gpu.comment {wsId, captureId, text}`, `gpu.pin {wsId, captureId, pinned}` | the library |
| `gpu.open {wsId, captureId}` | the capture's replay session (started, or the running one): `{sessionId}`; its state on `gpurs:<sessionId>` |
| `gpu.replay {sessionId, method, params}` | one `ied-gpu/1` request; a binary part comes back as `blob` (a blob id, `GET /api/blob/<id>`) |
| `gpu.close {sessionId}` | ends the session for everyone |

Topics: `gpu:<wsId>` (`{captures, targets}`; events `captures.changed`, `target.changed`, `target.removed`), `gpurs:<sessionId>` (`{state: starting|ready|failed|ended, summary, problem}`). Sessions end 30 minutes after their last request, and when the replayer exits (a crash shows on the topic, and the next `gpu.open` starts it again).

## Views

PIX's structure, restyled with IED's tokens: a capture opens as a **document tab** (several captures side by side for compare); each capture document has **view tabs** - Overview, Pipeline, Debug, Performance, Compare; a **timing timeline** runs across the top (marker rows, one row per queue, counter plots); the **events grid** is on the left in every view; panes are dense grids with VS-style docked tool windows at the bottom (Warnings, Pixel History, Resource History, API Calls, Messages). The activity's start page is the capture library.

| View | Contents |
|---|---|
| Capture library | captures per workspace with thumbnail, program, config, git commit, frame, GPU, size, backend; comments; pin; compare; the running targets with Capture Next Frame |
| Events grid | markers as tree rows, API calls as rows (`DrawIndexedInstanced(36, 1, 0, 0, 37)` with the object name), columns EID, GPU ms with an in-cell bar, primitives, PS invocations; warning badges; bookmarks; filter language |
| Timing timeline | marker rows (frame, passes, draws), one row per queue (Direct, Compute, Copy, Video) with idle bubbles, counter plots (GPU busy, occupancy, bandwidth), range selection |
| Pipeline view | state tree by stage (Input Assembler ... Output Merger, Root Signature, Descriptor Heaps) with every binding; bindless indices resolved with names; constant buffers decoded with the shader's reflection; selecting an item shows it in the resource viewer |
| Resource viewer | the selected texture or buffer: mip/slice/sample, channels, range, *Visualize* menu (NaN/Inf/negative, depth/stencil test, overdraw, wireframe, triangle size, highlight draw), zoom; a status line with the pixel, typed value, depth and last write; Pixel History / Debug Pixel / Resource History actions |
| Buffer view | the selected buffer after the event, a page of elements at a time from an offset, read with a layout: `float3 position; float3 normal; float2 uv` - fields in order, tightly packed, each a type (`float`, `half`, `double`, `int`, `uint`, `int8/16/64`, `uint8/16/64`, `unorm8/16`, `snorm8/16`, `bool`; vectors `float3`, `uint16x2`; matrices `float4x3`, 3 vectors of 4 as stored), an optional name and `[count]`; `pad N` skips bytes; `hex` shows 16 bytes a row with their text. A binding suggests the layout: a vertex buffer its attributes, an index buffer its format, a constant, uniform or storage buffer its block as the shaders reflect it (SPIR-V's offsets and strides, HLSL's cbuffer packing; padded array elements and matrix columns become a field each), a storage buffer that is one runtime array its element; a constant buffer shows as its one element's fields, a line per vector. An edited layout holds for that buffer and binding while the capture is open |
| Mesh Viewer | VS input/output, mesh/task output, table + 3D preview with camera from the capture |
| Resources | all resources with usage, format, size, lifetime (first/last EID), memory heap, aliasing; per-resource history of writes and reads |
| API Calls | the calls inside the selected event, parameters, callstack if captured |
| Pixel History | every event that touched a pixel: before/after values, shader output, which test failed (depth, stencil, scissor, culling, blend), plain-words summary |
| Shader Debugger | source in an editor surface (LSP from the shader-language plugin), breakpoints, step in/over/out (and back), watch, locals, per-lane view, disassembly toggle |
| Messages | debug layer output tied to EIDs |
| Performance | passes grid with A/B columns (A ms, B ms, delta) and statistics, counters grid for the selected range with throughput bars and a limiter verdict, Analysis findings, the timeline with counter plots |
| Warnings / Analysis | one grid for debug-layer messages and IED's analysis (NaN writes, regressions, culling, barriers, overdraw, queue bubbles), each row linked to its EID with Ask AI |

Phone (one pane at a time, `docs/15`): capture library, event list with timings, texture preview with picks, pixel history, insights and AI answers. Shader stepping and the mesh viewer are desktop-only.

## Capture flow

- **Launch:** *Capture* on the toolbar (next to *Profile*) and GPU > Launch for Capture build the startup project and start the program its debug configuration names with the capture environment: `FLASHBANG_DLL` (where the capture library is), `FLASHBANG_CAPTURE_DIR`, and the Vulkan layer's variables. A Vulkan program is captured as it is; a D3D12 program loads the library and wraps its device and factory through `Gpu::Capture` (or `flashbang_app.h`). *Capture Next Frame* (the GPU activity, the command palette, any device) asks the running program for a frame.
- **From code:** Bedrock `Gpu::Capture` (`C4/Bedrock/Gpu/`, opt-in `include "gpu.ch"`): `Load()` loads the capture library (from `FLASHBANG_DLL`, or a path), `WrapD3D12Device` / `WrapDxgiFactory`, `TriggerCapture(n)`, `StartFrame` / `EndFrame` for programs without a swap chain, `SetComment`, `Annotate(key, value)` for the metadata chunk. Without the library every call does nothing (the wraps return what they were given), so a program can leave them in.
- **From the profiler:** "capture on hitch" and "capture this frame" on a GPU zone (later); marker names are shared with `Profile::Gpu::Zone`, which emits the native markers with the zone's name.
- **Storage:** captures live under `.ied/local/gpu-captures/` (gitignored; the profiler keeps its own in `.ied/local/captures/`) with their sidecars; the library indexes them. Pin a capture to keep it past the retention limit (`gpu.keepCaptures`).

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
| G1 | Flashbang capture and replay for D3D12 and Vulkan (the common subset of both APIs: graphics, compute, copies, markers, bindless descriptor heaps / descriptor indexing); launch for capture; library; Events grid, timeline, Pipeline view, resource viewer with picks, Resources, API Calls, Messages, Mesh Viewer (VS input); per-event timing and pipeline statistics; Bedrock `Gpu::Capture`; read-only AI tools | process spawn + blobs (M1), tasks (M3), docking (M2) |
| G2 | Pixel History, Performance view with counter providers, compare and insights, Edit and Replay, phone triage, capture on hitch with the profiler, VS output / mesh shader output | G1, profiler (M9) |
| G3 | Shader debugging by instrumented replay (time travel), work graphs, ray tracing, rolling and multi-frame captures, `Gpu::Annotate` bindless naming | G2 |

Roadmap placement: `docs/18` M12 (G1-G2) and M13 (G3).

## Status (2026-10-21)

Built (G1 for D3D12 and Vulkan), in `plugins/gpu/`:

- **Capture** (`native/capture/`): the D3D12 proxies, the state and descriptor tracking, initial contents, the frame
  recording, the `.flash` writer with its sidecar and thumbnail (only when a frame was presented), the control pipe, and
  frame bounds from `Present` or from `StartFrame` / `EndFrame` (each `StartFrame` is a frame, captured when one was
  triggered). The C/C++ helper `native/include/flashbang_app.h`. The Vulkan layer (`VK_LAYER_IED_flashbang`) with the
  same coverage: object and layout tracking, descriptor shadows, mapped-memory diffs, initial contents, the thumbnail.
- **Replay** (`native/replay/`): `flashbang-replay --rpc` answers `initialize`, `events`, `apiCalls`, `state`,
  `resources`, `texture`, `pixel`, `buffer`, `mesh`, `shader`, `timing` and `messages` on `ied-gpu/1`; its CLI does the
  same for scripts and tests, for D3D12 and Vulkan captures alike.
- **Server** (`code/`, namespace `GpuDebug`): the `gpu.*` methods and the `gpu:` / `gpurs:` topics above, Launch for
  Capture from the startup project's debug configuration (its pre-launch task first), one replayer per open capture
  (ended with its capture's delete, after 30 idle minutes, or with the server).
- **UI** (`ui/`): the Capture Library tool window in the GPU activity; a capture as a document with Overview, Pipeline
  and Performance; the timeline, the events grid with its filter, the state tree, the resource viewer (channels, range,
  NaN / Inf / negative visualizing, mips and slices, picks), the buffer view, the shader and Mesh panels, and the API Calls, Messages,
  Resources and Warnings grids; at phone width the events below one of Output, State or Calls.
- **C4:** Bedrock `Gpu::Capture` (`C4/docs/library/bedrock.md` section 18) and the headless sample
  `plugins/gpu/samples/c4`.
- **Checks:** `nuke test flashbang` (`flashbang-tests`, and `native/tests/test_flashbang.py`, which captures the D3D12
  sample, the C4 sample and the Vulkan sample through the layer and replays them, the Vulkan frame compared with the
  program's own screenshot; the C4 one needs a toolchain whose Bedrock has `gpu.ch`), `tools/server-checks/tests/test_gpu.py`
  (launch, capture, open, replay, blobs, comment, delete, refusals; a Vulkan launch through the layer), Bedrock
  `gpu_tests.cf`.

Not yet: the AI tools; Pixel History and the rest of G2.

## Risks

- Replay is driver-sensitive: replay on the capturing GPU by default, show the capture's GPU/driver beside the replay's, and keep the replayer restartable.
- API coverage is ours to grow: a call the capture layer does not understand is passed through and named in the capture's warnings, so a capture never silently lies about what it lacks.
- Captures are large (GBs): stream from disk, decode lazily, retention limits.
- Vendor counter SDKs differ in licensing and coverage: providers are optional plugins; D3D12 timestamps and pipeline statistics (and Vulkan's) always work.
- The server needs the GPU: the replay runs where the IED server runs (the owner's desktop); the phone only views.
