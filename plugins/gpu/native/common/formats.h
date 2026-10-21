// formats.h - texture formats: what each DXGI (and Vulkan, through vk_formats) format stores, and its texels decoded to
// float RGBA for the resource viewer (docs/22: the browser gets RGBA32F; pixel picks keep the exact typed value).
#pragma once

#include "fb_base.h"

#include <dxgiformat.h>

namespace fb
{
	enum class Component : u8 { None, UNorm, SNorm, UInt, SInt, Float, Srgb, Typeless };

	enum class Codec : u8
	{
		Unknown,
		Channels,        // `channels` components of `bits` each, in RGBA order (or BGRA with bgr)
		R10G10B10A2,
		R11G11B10F,
		R9G9B9E5,
		B5G6R5,
		B5G5R5A1,
		B4G4R4A4,
		D24S8,           // depth 24 unorm, stencil 8 (R24G8 family)
		D32S8X24,        // depth float, stencil 8, 24 unused (R32G8X24 family)
		BC1, BC2, BC3, BC4, BC5, BC6H, BC7,
		R8G8_B8G8, G8R8_G8B8,
		R10G10B10_XR_A2,
	};

	struct FormatInfo
	{
		u32 format;          // DXGI_FORMAT (or VkFormat for Vulkan formats)
		const char* name;
		Codec codec;
		Component component;
		u8 channels;
		u8 bits;             // per channel, for Codec::Channels
		u8 bytes;            // per texel, or per 4x4 block for block formats
		bool bgr;            // stored B, G, R(, A)
		bool depth;
		bool stencil;
	};

	bool IsBlock(Codec codec);
	const FormatInfo* DxgiFormat(DXGI_FORMAT format);
	const char* DxgiFormatName(DXGI_FORMAT format);
	// The format a typeless one is viewed as by default (R8G8B8A8_TYPELESS -> R8G8B8A8_UNORM); others unchanged.
	DXGI_FORMAT DxgiDefaultView(DXGI_FORMAT format);
	// The format of one plane of a planar depth-stencil format (plane 0 depth, 1 stencil) as copied to a buffer.
	DXGI_FORMAT DxgiPlaneFormat(DXGI_FORMAT format, u32 plane);

	// Decodes `width` x `height` texels of `info` from rows at `src` (`rowPitch` bytes apart; block rows for block
	// formats) into RGBA floats (`out`, width * height * 4). Missing channels read 0 (alpha 1).
	void DecodeRows(const FormatInfo& info, const u8* src, size_t rowPitch, u32 width, u32 height, float* out);
	// One texel's typed components as text and numbers: `values` gets up to 4 (integers exact as doubles).
	void TexelValues(const FormatInfo& info, const u8* texel, double values[4], u32& count);

	float HalfToFloat(u16 half);
	u16 FloatToHalf(float value);
}
