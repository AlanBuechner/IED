// The buffer view's layouts the replayers write (common/bufferlayout.cpp): a format's field type, a binding's
// attributes at their offsets with the bytes between and after them as padding, and a reflected block's members.
#include "testing.h"

#include "bufferlayout.h"

using namespace fb;

TEST(FieldOfNamesTheFormatsType) {
	CHECK(FieldOf(DxgiFormat(DXGI_FORMAT_R32G32B32_FLOAT), "position") == "float3 position");
	CHECK(FieldOf(DxgiFormat(DXGI_FORMAT_R32_FLOAT), "depth") == "float depth");
	CHECK(FieldOf(DxgiFormat(DXGI_FORMAT_R8G8B8A8_UNORM), "color") == "unorm8x4 color");
	CHECK(FieldOf(DxgiFormat(DXGI_FORMAT_R16G16_FLOAT), "uv") == "half2 uv");
	CHECK(FieldOf(DxgiFormat(DXGI_FORMAT_R16_UINT), "index") == "uint16 index");
	CHECK(FieldOf(DxgiFormat(DXGI_FORMAT_R32_TYPELESS), "value") == "uint value");
	CHECK(FieldOf(DxgiFormat(DXGI_FORMAT_R10G10B10A2_UNORM), "packed") == "uint packed");
	CHECK(FieldOf(DxgiFormat(DXGI_FORMAT_BC1_UNORM), "block") == "uint8 block[8]");
	CHECK(FieldOf(nullptr, "nothing").empty());
}

TEST(BufferLayoutPadsBetweenFields) {
	// Out of order, with a gap, an overlapping field and room left at the end of the stride.
	const std::string layout = BufferLayout({{16, 8, "float2 uv"}, {0, 12, "float3 position"}, {8, 4, "float alias"}}, 32);
	CHECK(layout == "float3 position; pad 4; float2 uv; pad 8");
	CHECK(BufferLayout({{0, 12, "float3 position"}}, 12) == "float3 position");
	CHECK(BufferLayout({}, 16).empty());
}

TEST(BlockLayoutPlacesReflectedMembers) {
	BlockMember viewProj;
	viewProj.name = "viewProj", viewProj.vectorType = "float4", viewProj.vectorBytes = 16, viewProj.vectors = 4, viewProj.vectorStride = 16;
	viewProj.elementStride = 64, viewProj.bytes = 64;
	BlockMember light;
	light.name = "light", light.vectorType = "float4", light.vectorBytes = 16, light.vectorStride = 16, light.elementStride = 16, light.offset = 64, light.bytes = 16;
	CHECK(BlockLayout({viewProj, light}, 80) == "float4x4 viewProj; float4 light");

	// HLSL and std140 put each array element and matrix column on a 16-byte register.
	BlockMember weights;
	weights.name = "weights", weights.vectorType = "float", weights.vectorBytes = 4, weights.vectorStride = 4, weights.elements = 3, weights.elementStride = 16, weights.bytes = 36;
	CHECK(BlockLayout({weights}, 48) == "float weights_0; pad 12; float weights_1; pad 12; float weights_2; pad 12");
	BlockMember basis;
	basis.name = "basis", basis.vectorType = "float3", basis.vectorBytes = 12, basis.vectors = 3, basis.vectorStride = 16, basis.elementStride = 48, basis.bytes = 44;
	CHECK(BlockLayout({basis}, 48) == "float3 basis_0; pad 4; float3 basis_1; pad 4; float3 basis_2; pad 4");
	BlockMember tight;
	tight.name = "colors", tight.vectorType = "float4", tight.vectorBytes = 16, tight.vectorStride = 16, tight.elements = 2, tight.elementStride = 16, tight.bytes = 32;
	CHECK(BlockLayout({tight}, 32) == "float4 colors[2]");

	// A struct reads as dwords; a runtime array is the buffer's rows, not part of the block.
	BlockMember material;
	material.name = "material", material.offset = 0, material.bytes = 32;
	BlockMember particles;
	particles.name = "particles", particles.vectorType = "float4", particles.vectorBytes = 16, particles.elements = 0, particles.offset = 32;
	CHECK(BlockLayout({material, particles}, 32) == "uint material[8]");
	CHECK(VectorType("uint16", 2) == "uint16x2" && VectorType("float", 3) == "float3" && VectorType("half", 1) == "half");
}
