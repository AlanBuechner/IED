// bufferlayout.h - the layout text the buffer view reads a buffer's elements with (docs/22 "Buffer view"):
// "float3 position; float3 normal; float2 uv", a field per element member, "pad N" for bytes between them. The
// replayers give a bound buffer one from what binds it: a vertex buffer's attributes, an index buffer's format.
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
}
