// The GPU capture client: `include "gpu.ch"` (bedrock.ch does not include it). Everything is in `namespace Gpu`
// (Gpu/capture.ch): Gpu::Capture loads IED's capture library when the program was started for capture, wraps a D3D12
// device and DXGI factory, and triggers captures and frame bounds; without the library every call gives back what it
// was given.

include "Gpu/capture.ch"
