---
name: gpu
description: Debug a frame a D3D12 or Vulkan program rendered, with IED's GPU debugger - read captures with the gpu_* tools (events, pipeline state, textures and pixels, buffers, shaders, NaN scans, GPU timing), and make a program capturable from C4, C or C++.
keywords: [gpu, graphics, render, rendering, frame, capture, d3d12, directx, dx12, vulkan, shader, hlsl, spir-v, draw call, render target, texture, pixel, nan, black screen, artifact, flicker, gpu time, slow frame, pix, renderdoc]
---

# GPU debugger

IED's GPU debugger captures one frame of a D3D12 or Vulkan program - every command it sent, every resource's contents
at the frame's start - and replays it on the server, stopping at any event. Captures live in the workspace's
`.ied/local/gpu-captures/` as `.flash` files. The owner sees them in the GPU activity (PIX's layout); you read them
with the `gpu_*` tools, which answer from the same replay.

Words the tools use:

- **EID** - an event's number in the frame. Markers (debug groups the program named) group the events after them.
- **Action** - an event that does work: a draw, dispatch, copy, clear or resolve. State is what the actions see.
- **Resource id** - `[#9]` in the answers: a texture or buffer. What an event writes is marked `rt` (render target),
  `depth` or `uav` (storage).

## Reading a frame

1. `gpu_captures` - the captures, newest first. Every other tool takes `capture` (default `"latest"`). With none
   yet, or to see a change you made, `gpu_capture` builds and runs the startup project, captures a frame and stops it.
2. `gpu_events` - the frame's events under their markers. Narrow with `filter` (`"Shadow"`, `"kind:draw Cube"`).
3. `gpu_event_state` at a draw - its shaders, vertex and index buffers, render targets, blend, depth and stencil,
   and every binding resolved to its resource and name. A marker's EID gives its last action's state.
4. `gpu_texture` at an EID - the texture's statistics and **the image**: what the target looked like right after
   that event. Compare an EID before and after a draw to see what the draw did.
5. `gpu_pixel` - one texel's stored value, when the exact number matters.
6. `gpu_buffer` - a buffer's bytes as floats, uints, ints or hex, from a binding's offset (gpu_event_state gives it).
7. `gpu_shader` - a stage's reflection and disassembly (DXBC, DXIL or SPIR-V): which registers or bindings it reads.
8. `gpu_timing` - where the GPU time goes, by pass or by the costliest draws, measured on the replay GPU.

## Common questions

- **Something is black, missing or wrong-colored.** Find the pass in `gpu_events`, then `gpu_texture` after its
  draws: the first EID where the image is wrong is the culprit. Check that draw's state: is the right target bound,
  is blending or the depth test throwing the pixels away (`Depth: LESS, write on` against a cleared depth of 0), is
  the viewport or scissor zero, are the vertex buffers the expected ones (`gpu_buffer` with the binding's offset)?
- **NaN or Inf (black or white speckles, a frame that turns black).** `gpu_scan` finds the first action after which
  the texture holds them; `gpu_texture` there says where; `gpu_shader` and the constant buffers (`gpu_buffer`) of
  that draw usually show the division by zero, the normalize of a zero vector, or the uninitialized constant.
- **Too slow.** `gpu_timing` by pass, then by event for the costly pass. High pixel-shader invocations against the
  target's size mean overdraw; many primitives in a small pass mean geometry that could be culled.

Say which capture and EIDs you looked at, so the owner can open the same events.

## Making a program capturable

The owner runs **GPU > Launch for Capture** (Alt+F5): IED builds the startup project and starts the program its debug
configuration names, with the capture environment. **Capture Next Frame** then grabs the next frame.

- **Vulkan** programs need nothing: IED's launch enables the `VK_LAYER_IED_flashbang` layer through the loader.
- **D3D12** programs opt in: right after creating the device and the DXGI factory, hand them to the capture library,
  which gives back instrumented ones (and the same objects when the library is not loaded, so the calls can stay in
  every build but Dist).
  - C4: `include "gpu.ch"`; keep a `Gpu::Capture capture;` for the program's life, `capture.Load()` at startup, then
    `device = capture.WrapD3D12Device(device)` and `factory = capture.WrapDxgiFactory(factory)` (Bedrock,
    `C4/docs/library/bedrock.md` section 18).
  - C or C++: `#include "flashbang_app.h"` (`plugins/gpu/native/include/`): `flashbang::Load(capture)`,
    `device = flashbang::WrapDevice(capture, device)`, `factory = flashbang::WrapFactory(capture, factory)`.
- A program without a swap chain marks its frames with `capture.StartFrame()` / `EndFrame()`; `TriggerCapture(n)`
  captures from the program itself (a key or console command). Name passes with the API's debug markers
  (`BeginEvent` / `vkCmdBeginDebugUtilsLabelEXT`) and objects with `SetName` / `vkSetDebugUtilsObjectNameEXT`: the
  events and state answers use those names.
