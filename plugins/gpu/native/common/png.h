// png.h - PNG files of RGBA8 images (thumbnails, exported textures), deflate's stored blocks: bigger than a
// compressed PNG, needing no compressor.
#pragma once

#include "fb_base.h"

namespace fb
{
	std::vector<u8> EncodePng(u32 width, u32 height, const u8* rgba, size_t rowPitch);
	bool WritePng(const std::wstring& path, u32 width, u32 height, const u8* rgba, size_t rowPitch);
	u32 Crc32(const u8* data, size_t size, u32 crc = 0);
}
