// The buffer view's layouts the replayers write (common/bufferlayout.cpp): a format's field type, and a binding's
// attributes at their offsets with the bytes between and after them as padding.
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
