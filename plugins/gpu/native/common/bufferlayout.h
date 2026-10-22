// bufferlayout.h - the layout text the buffer view reads a buffer's elements with (docs/22 "Buffer view"):
// "float3 position; float3 normal; float2 uv", a field per element member, "pad N" for bytes between them. The
// replayers give a bound buffer one from what binds it: a vertex buffer's attributes, an index buffer's format, a
// constant buffer's block as the shader reflects it.
#pragma once

#include "formats.h"

#include <string>
#include <vector>

namespace fb
{
	struct LayoutField
	{
		u32 offset;
		u32 bytes;
		std::string text;      // "float3 position"
	};

	// The field a value of `info` reads as: "float3 name", "unorm8x4 name", or "uint8 name[N]" for a packed or block
	// format; empty without a format.
	std::string FieldOf(const FormatInfo* info, const std::string& name);
	// The fields in offset order with "pad N" between them and up to `stride`; a field overlapping the one before it is
	// left out.
	std::string BufferLayout(std::vector<LayoutField> fields, u32 stride);

	// A block member as shader reflection places it: `vectors` vectors of `vectorType` (a matrix's columns, or its rows
	// when row-major) `vectorStride` bytes apart, `elements` times `elementStride` bytes apart. A member without a
	// vector type (a struct) reads as its bytes in dwords.
	struct BlockMember
	{
		std::string name;
		std::string vectorType;    // a layout type: "float4", "uint", "half2"
		u32 vectorBytes = 0;
		u32 vectors = 1;
		u32 vectorStride = 0;
		u32 elements = 1;
		u32 elementStride = 0;
		u32 offset = 0;
		u32 bytes = 0;             // the whole member
	};

	// The layout type of `components` components of a base ("float" 3 -> "float3", "uint16" 2 -> "uint16x2").
	std::string VectorType(const std::string& base, u32 components);
	// A block's layout: tight arrays and matrices as one field ("float4x4 world", "float4 lights[4]"), padded ones (HLSL
	// and std140 arrays of scalars, matrix columns) as a field per element and vector with the padding between; empty
	// when the block would take more than 512 fields.
	std::string BlockLayout(const std::vector<BlockMember>& members, u32 blockBytes);
}
