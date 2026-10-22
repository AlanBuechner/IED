// texels.h - what both replays do with a subresource read back: the resource viewer's RGBA32F image (box-filtered down
// to a size, a NaN or Inf in a box kept visible, with each channel's range and the NaN / Inf / negative counts) and one
// texel's typed values for a pixel pick.
#pragma once

#include "formats.h"
#include "json.h"

namespace fb::replay
{
	// Writes width, height, srcWidth, srcHeight, step, min, max, nan, inf, negative and encoding to `out`, and the
	// image (RGBA32F, rows of the shown width) to `binary`.
	void ShowTexels(const FormatInfo& info, const u8* data, size_t rowPitch, u32 width, u32 height, u32 maxSize, JsonWriter& out, std::vector<u8>& binary);
	// Writes values, text and (for uncompressed formats) raw for the texel at (x, y).
	void PixelValues(const FormatInfo& info, const u8* data, size_t rowPitch, u32 x, u32 y, JsonWriter& out);
}
