// Shader debugging for D3D12 (docs/22 "Shader debugging"): DXBC instrumentation is the next piece of G2; until it
// lands the request says so.
#include "d3d12replay.h"

namespace fb::replay::d3d12
{
	void D3D12Replay::DebugShader(const JsonValue& params, JsonWriter& out) {
		Fail("shader debugging is built for Vulkan (SPIR-V) so far; D3D12 (DXBC, then DXIL) comes next");
	}
}
