# Features: GPU debugger (GPU)

Owner: `plugins/gpu` (builtin plugin: library, sessions, insights, compare, AI tools, UI module) with its own backend, Flashbang, under `plugins/gpu/native/` (the capture library and Vulkan layer, and the replayer, one process per open capture; owner decision 2026-10-21, `docs/99` #23: RenderDoc is a reference, not a dependency). Capture-side support for C4 programs is Bedrock `Gpu::` in the C4 tree (`C4/Bedrock/`). Design: `docs/22`. Mocks: `frontend/mocks/07-gpu-frame.html`, `08-gpu-shader-debug.html`, `09-gpu-performance.html`.

PIX is the UI reference - layout and styling (owner decision 2026-10-19; RenderDoc's look is not wanted) - RenderDoc the inspection and openness reference, Nsight the performance reference; the "Beyond" column says what IED adds.

## Capture and library

| ID | Feature | Pri | Stories | Beyond |
|---|---|---|---|---|
| F-GPU-1 | GPU activity with a capture library: thumbnail, program, config, git commit, frame number, capture GPU and driver, backend, size; comments, pin, retention limits | P1 | US-GPU-1 | commit and build metadata on every capture |
| F-GPU-2 | Launch for Capture: build, start the startup project with the capture library, capture the next frame (IED command from any device, the program's own key through `Gpu::Capture::TriggerCapture`, or N frames) | P1 | US-GPU-1 | capture from the phone |
| F-GPU-3 | Programmatic capture from code through Bedrock `Gpu::Capture` (wrap the device and factory, trigger, frame bounds for programs without a swap chain, comments and annotations) | P1 | US-GPU-1 | the calls stay in every build: without the library they pass through |
| F-GPU-4 | Hand-off: open a capture in RenderDoc's UI; capture with PIX and open in PIX; launch the program in Nsight Graphics | P2 | US-GPU-1 | - |
| F-GPU-5 | Capture on hitch: a profiler frame-time trigger captures the next frame, or writes the rolling frames around the spike | P2 | US-GPU-9 | none of the three tie GPU capture to a CPU profiler |

## Events and timeline

| ID | Feature | Pri | Stories | Beyond |
|---|---|---|---|---|
| F-GPU-10 | Events grid (PIX-style): markers as tree rows, API calls as rows with the object name, columns EID / GPU ms with in-cell bar / primitives / PS invocations, warning badges, bookmarks, a filter language (`kind:draw ps:DefaultLit rt:HDR time>0.5ms`) | P1 | US-GPU-1 | bookmarks and filter language inside PIX's grid |
| F-GPU-11 | API Calls for the selected event with parameters and optional callstacks | P1 | US-GPU-1 | - |
| F-GPU-12 | Timing timeline across the top of every view: marker rows, one row per queue (Direct/Compute/Copy) with fences and idle bubbles, counter plots, range selection | P1 | US-GPU-3 | - |
| F-GPU-14 | Capture document with view tabs Overview / Pipeline / Debug / Performance / Compare; several captures open as document tabs | P1 | US-GPU-1 | - |
| F-GPU-13 | Marker names shared with the profiler (`Profile::Gpu::Zone`); GPU zone -> capture event and back | P1 | US-GPU-1 | one vocabulary for CPU profiler and GPU debugger |

## State and resources

| ID | Feature | Pri | Stories | Beyond |
|---|---|---|---|---|
| F-GPU-20 | Pipeline view (PIX-style): state tree by stage (graphics, mesh, compute, ray tracing, work graph variants) with every binding; selecting an item shows it in the resource viewer | P1 | US-GPU-1 | - |
| F-GPU-21 | Bindings: root signature, descriptor tables and heaps; bindless indices resolved to the resource, its name and engine annotations | P1 | US-GPU-1 | bindless-first display (the example engine is bindless) |
| F-GPU-22 | Constant buffer contents decoded with shader reflection (structs, arrays, root constants) | P1 | US-GPU-1 | - |
| F-GPU-23 | Resources: usage, format, size, lifetime (first/last EID), heap and aliasing; per-resource read/write history | P1 | US-GPU-1 | - |
| F-GPU-24 | Messages: debug layer and GPU-based validation output tied to EIDs | P1 | US-GPU-1 | - |

## Textures, buffers, meshes

| ID | Feature | Pri | Stories | Beyond |
|---|---|---|---|---|
| F-GPU-30 | Resource viewer (PIX-style): channels, range, mip/slice/sample, zoom; status line with pixel, typed value, depth and last write; Pixel History / Debug Pixel / Resource History actions | P1 | US-GPU-1 | - |
| F-GPU-31 | Display transforms in the browser (WebGL2) over decoded tiles: instant range/channel/gamma changes, NaN/Inf/negative highlight, on any device | P1 | US-GPU-1, US-GPU-7 | remote viewing without round trips per slider move |
| F-GPU-32 | *Visualize* menu rendered by the backend: NaN/Inf/negative, highlight draw, wireframe, depth test, stencil test, backface, viewport/scissor, quad overdraw (draw/pass), triangle size | P1 | US-GPU-1 | RenderDoc's overlays inside PIX's viewer |
| F-GPU-33 | Mesh Viewer: VS input/output, task/mesh output, table + WebGL2 3D preview | P1 | US-GPU-1 | - |
| F-GPU-34 | Buffer viewer with a format language (struct layout from reflection or typed by hand) | P1 | US-GPU-1 | - |
| F-GPU-35 | Export a resource (DDS, PNG, EXR, raw) and open it in the image viewer | P2 | US-GPU-1 | - |

## Pixel and shader debugging

| ID | Feature | Pri | Stories | Beyond |
|---|---|---|---|---|
| F-GPU-40 | Pixel History: every event that touched a pixel with before/after, shader output, the test that failed, and a plain-words summary | P1 | US-GPU-2 | the summary sentence; built 2026-10-21 but the shader output |
| F-GPU-41 | Shader debugger for a pixel, vertex or compute thread: source-level (PDB or embedded debug info), step in/over/out, breakpoints set in the editor, watch, locals, per-lane values, disassembly toggle | P1 | US-GPU-2 | stepping inside an IED editor surface with LSP; built 2026-10-21 for Vulkan and D3D12 DXBC (Shader Model 5.1) at the disassembly level |
| F-GPU-42 | Edit and Replay: edit the shader in the editor, recompile with the shader language plugin's compiler, replay, show an output diff; save as a real file change | P1 | US-GPU-8 | output diff, and the edit is a normal document change |
| F-GPU-43 | Time-travel shader debugging (step backward) by instrumented replay on the real GPU | P2 | US-GPU-9 | none of the three step backward; built with F-GPU-41 |
| F-GPU-44 | NaN/Inf hunt: scan a target across events and bisect the first event that wrote a bad value | P1 | US-GPU-5 | automated |

## Performance

| ID | Feature | Pri | Stories | Beyond |
|---|---|---|---|---|
| F-GPU-50 | Per-event and per-pass GPU timing (timestamp queries, N repeats, median) | P1 | US-GPU-3 | - |
| F-GPU-51 | Pipeline statistics per event (invocations, primitives, clipping) | P1 | US-GPU-3 | - |
| F-GPU-52 | Counter providers (AMD GPA, NVIDIA Perf SDK, Intel) as optional plugins; speed-of-light bars per unit, occupancy, limiter verdict | P2 | US-GPU-3 | vendor-neutral view |
| F-GPU-53 | Insights: redundant barriers, overdraw hot spots, tiny draws, empty passes, unused render-target writes, large uploads, queue bubbles - each with "Ask AI" | P1 | US-GPU-5 | AI explains and proposes the fix |

## Compare

| ID | Feature | Pri | Stories | Beyond |
|---|---|---|---|---|
| F-GPU-60 | Capture compare: event alignment by marker path and call signature, state diff, output image diff, timing deltas per pass | P1 | US-GPU-6 | not offered by PIX, RenderDoc or Nsight |
| F-GPU-61 | Visual-regression bisect across a commit range (the orchestration queue builds, captures and compares) | P2 | US-GPU-6 | - |

## AI and integration

| ID | Feature | Pri | Stories | Beyond |
|---|---|---|---|---|
| F-GPU-70 | AI tools `T-GPU-*` (`docs/13`) | P1 | US-GPU-5 | T-GPU-1, 2, 3, 6, 7, 9 and 11-14 built 2026-10-21 |
| F-GPU-71 | Chat attachments `@gpu:` capture, event, pixel, resource; GPU tool cards render thumbnails and value tables | P1 | US-GPU-5 | - |
| F-GPU-72 | "Explain this event / pixel / pass" on context menus | P1 | US-GPU-5 | - |
| F-GPU-73 | Golden-capture tests (pixel assertions, image compare) through the test provider binding | P2 | US-GPU-6 | - |
| F-GPU-80 | Phone triage: library, event list with timings, texture preview and picks, pixel history, insights, AI answers | P2 | US-GPU-7 | - |

## Backends

| ID | Feature | Pri | Stories | Beyond |
|---|---|---|---|---|
| F-GPU-90 | `ied-gpu/1`: JSON-RPC with binary parts over stdio, binary parts handed to the browser as blobs; one replayer per open capture; crash restart without losing UI state | P1 | US-GPU-4 | - |
| F-GPU-91 | Dropped 2026-10-21 (`docs/99` #23): a RenderDoc backend - Flashbang captures and replays | - | - | - |
| F-GPU-92 | Flashbang capture for any program without injection: D3D12 by handing the device to the library (Bedrock `Gpu::Capture`, `flashbang_app.h`), Vulkan through the loader's layer; metadata chunk (bindless names, entities, materials), rolling capture | P1 | US-GPU-9 | engine semantics in the capture |
| F-GPU-93 | Flashbang replayer implementing `ied-gpu/1` (D3D12 and Vulkan built; next: work graphs, unbounded/bindless resources) | P1 | US-GPU-9 | covers what RenderDoc does not |
| F-GPU-94 | Capture GPU/driver shown beside the replay GPU/driver; replay device selection | P1 | US-GPU-1 | - |
