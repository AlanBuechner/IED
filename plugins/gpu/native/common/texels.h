// texels.h - what both replays do with a subresource read back: the resource viewer's RGBA32F image (box-filtered down
// to a size, a NaN or Inf in a box kept visible, with each channel's range and mean and the NaN / Inf / negative counts),
// that image as a PNG for a model to look at, and one texel's typed values for a pixel pick.
#pragma once

#include "formats.h"
#include "json.h"

namespace fb::replay
{
	// Writes width, height, srcWidth, srcHeight, step, min, max, mean, nan, inf, negative, firstBad (the first NaN or Inf
	// texel's {x, y}, when there is one) and encoding to `out`, and the image (RGBA32F, rows of the shown width) to
	// `binary`.
	void ShowTexels(const FormatInfo& info, const u8* data, size_t rowPitch, u32 width, u32 height, u32 maxSize, JsonWriter& out, std::vector<u8>& binary);
	// The shown image (ShowTexels' answer `shown`, with the API's `linear`, and its RGBA32F `rgba`) as an 8-bit PNG at
	// `path`: [lo, hi] spans the 8 bits, NaN reads 0, linear values are sRGB-encoded, alpha is opaque.
	bool SaveShownPng(const JsonValue& shown, const std::vector<u8>& rgba, double lo, double hi, const std::wstring& path);
	// Writes values, text and (for uncompressed formats) raw for the texel at (x, y).
	void PixelValues(const FormatInfo& info, const u8* data, size_t rowPitch, u32 x, u32 y, JsonWriter& out);
}
