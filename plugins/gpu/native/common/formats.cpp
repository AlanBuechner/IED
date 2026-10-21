#include "formats.h"

#include <cmath>

namespace fb
{
	using C = Component;

#define F(fmt, codec, comp, ch, bits, bytes, bgr, depth, stencil) {(u32)DXGI_FORMAT_##fmt, #fmt, Codec::codec, C::comp, ch, bits, bytes, bgr, depth, stencil}

	static const FormatInfo kDxgi[] = {
		F(UNKNOWN, Unknown, None, 0, 0, 0, false, false, false),
		F(R32G32B32A32_TYPELESS, Channels, Typeless, 4, 32, 16, false, false, false),
		F(R32G32B32A32_FLOAT, Channels, Float, 4, 32, 16, false, false, false),
		F(R32G32B32A32_UINT, Channels, UInt, 4, 32, 16, false, false, false),
		F(R32G32B32A32_SINT, Channels, SInt, 4, 32, 16, false, false, false),
		F(R32G32B32_TYPELESS, Channels, Typeless, 3, 32, 12, false, false, false),
		F(R32G32B32_FLOAT, Channels, Float, 3, 32, 12, false, false, false),
		F(R32G32B32_UINT, Channels, UInt, 3, 32, 12, false, false, false),
		F(R32G32B32_SINT, Channels, SInt, 3, 32, 12, false, false, false),
		F(R16G16B16A16_TYPELESS, Channels, Typeless, 4, 16, 8, false, false, false),
		F(R16G16B16A16_FLOAT, Channels, Float, 4, 16, 8, false, false, false),
		F(R16G16B16A16_UNORM, Channels, UNorm, 4, 16, 8, false, false, false),
		F(R16G16B16A16_UINT, Channels, UInt, 4, 16, 8, false, false, false),
		F(R16G16B16A16_SNORM, Channels, SNorm, 4, 16, 8, false, false, false),
		F(R16G16B16A16_SINT, Channels, SInt, 4, 16, 8, false, false, false),
		F(R32G32_TYPELESS, Channels, Typeless, 2, 32, 8, false, false, false),
		F(R32G32_FLOAT, Channels, Float, 2, 32, 8, false, false, false),
		F(R32G32_UINT, Channels, UInt, 2, 32, 8, false, false, false),
		F(R32G32_SINT, Channels, SInt, 2, 32, 8, false, false, false),
		F(R32G8X24_TYPELESS, D32S8X24, Typeless, 2, 0, 8, false, true, true),
		F(D32_FLOAT_S8X24_UINT, D32S8X24, Float, 2, 0, 8, false, true, true),
		F(R32_FLOAT_X8X24_TYPELESS, D32S8X24, Float, 1, 0, 8, false, true, false),
		F(X32_TYPELESS_G8X24_UINT, D32S8X24, UInt, 2, 0, 8, false, false, true),
		F(R10G10B10A2_TYPELESS, R10G10B10A2, Typeless, 4, 0, 4, false, false, false),
		F(R10G10B10A2_UNORM, R10G10B10A2, UNorm, 4, 0, 4, false, false, false),
		F(R10G10B10A2_UINT, R10G10B10A2, UInt, 4, 0, 4, false, false, false),
		F(R11G11B10_FLOAT, R11G11B10F, Float, 3, 0, 4, false, false, false),
		F(R8G8B8A8_TYPELESS, Channels, Typeless, 4, 8, 4, false, false, false),
		F(R8G8B8A8_UNORM, Channels, UNorm, 4, 8, 4, false, false, false),
		F(R8G8B8A8_UNORM_SRGB, Channels, Srgb, 4, 8, 4, false, false, false),
		F(R8G8B8A8_UINT, Channels, UInt, 4, 8, 4, false, false, false),
		F(R8G8B8A8_SNORM, Channels, SNorm, 4, 8, 4, false, false, false),
		F(R8G8B8A8_SINT, Channels, SInt, 4, 8, 4, false, false, false),
		F(R16G16_TYPELESS, Channels, Typeless, 2, 16, 4, false, false, false),
		F(R16G16_FLOAT, Channels, Float, 2, 16, 4, false, false, false),
		F(R16G16_UNORM, Channels, UNorm, 2, 16, 4, false, false, false),
		F(R16G16_UINT, Channels, UInt, 2, 16, 4, false, false, false),
		F(R16G16_SNORM, Channels, SNorm, 2, 16, 4, false, false, false),
		F(R16G16_SINT, Channels, SInt, 2, 16, 4, false, false, false),
		F(R32_TYPELESS, Channels, Typeless, 1, 32, 4, false, false, false),
		F(D32_FLOAT, Channels, Float, 1, 32, 4, false, true, false),
		F(R32_FLOAT, Channels, Float, 1, 32, 4, false, false, false),
		F(R32_UINT, Channels, UInt, 1, 32, 4, false, false, false),
		F(R32_SINT, Channels, SInt, 1, 32, 4, false, false, false),
		F(R24G8_TYPELESS, D24S8, Typeless, 2, 0, 4, false, true, true),
		F(D24_UNORM_S8_UINT, D24S8, UNorm, 2, 0, 4, false, true, true),
		F(R24_UNORM_X8_TYPELESS, D24S8, UNorm, 1, 0, 4, false, true, false),
		F(X24_TYPELESS_G8_UINT, D24S8, UInt, 2, 0, 4, false, false, true),
		F(R8G8_TYPELESS, Channels, Typeless, 2, 8, 2, false, false, false),
		F(R8G8_UNORM, Channels, UNorm, 2, 8, 2, false, false, false),
		F(R8G8_UINT, Channels, UInt, 2, 8, 2, false, false, false),
		F(R8G8_SNORM, Channels, SNorm, 2, 8, 2, false, false, false),
		F(R8G8_SINT, Channels, SInt, 2, 8, 2, false, false, false),
		F(R16_TYPELESS, Channels, Typeless, 1, 16, 2, false, false, false),
		F(R16_FLOAT, Channels, Float, 1, 16, 2, false, false, false),
		F(D16_UNORM, Channels, UNorm, 1, 16, 2, false, true, false),
		F(R16_UNORM, Channels, UNorm, 1, 16, 2, false, false, false),
		F(R16_UINT, Channels, UInt, 1, 16, 2, false, false, false),
		F(R16_SNORM, Channels, SNorm, 1, 16, 2, false, false, false),
		F(R16_SINT, Channels, SInt, 1, 16, 2, false, false, false),
		F(R8_TYPELESS, Channels, Typeless, 1, 8, 1, false, false, false),
		F(R8_UNORM, Channels, UNorm, 1, 8, 1, false, false, false),
		F(R8_UINT, Channels, UInt, 1, 8, 1, false, false, false),
		F(R8_SNORM, Channels, SNorm, 1, 8, 1, false, false, false),
		F(R8_SINT, Channels, SInt, 1, 8, 1, false, false, false),
		F(A8_UNORM, Channels, UNorm, 1, 8, 1, false, false, false),
		F(R9G9B9E5_SHAREDEXP, R9G9B9E5, Float, 3, 0, 4, false, false, false),
		F(R8G8_B8G8_UNORM, R8G8_B8G8, UNorm, 3, 0, 4, false, false, false),
		F(G8R8_G8B8_UNORM, G8R8_G8B8, UNorm, 3, 0, 4, false, false, false),
		F(BC1_TYPELESS, BC1, Typeless, 4, 0, 8, false, false, false),
		F(BC1_UNORM, BC1, UNorm, 4, 0, 8, false, false, false),
		F(BC1_UNORM_SRGB, BC1, Srgb, 4, 0, 8, false, false, false),
		F(BC2_TYPELESS, BC2, Typeless, 4, 0, 16, false, false, false),
		F(BC2_UNORM, BC2, UNorm, 4, 0, 16, false, false, false),
		F(BC2_UNORM_SRGB, BC2, Srgb, 4, 0, 16, false, false, false),
		F(BC3_TYPELESS, BC3, Typeless, 4, 0, 16, false, false, false),
		F(BC3_UNORM, BC3, UNorm, 4, 0, 16, false, false, false),
		F(BC3_UNORM_SRGB, BC3, Srgb, 4, 0, 16, false, false, false),
		F(BC4_TYPELESS, BC4, Typeless, 1, 0, 8, false, false, false),
		F(BC4_UNORM, BC4, UNorm, 1, 0, 8, false, false, false),
		F(BC4_SNORM, BC4, SNorm, 1, 0, 8, false, false, false),
		F(BC5_TYPELESS, BC5, Typeless, 2, 0, 16, false, false, false),
		F(BC5_UNORM, BC5, UNorm, 2, 0, 16, false, false, false),
		F(BC5_SNORM, BC5, SNorm, 2, 0, 16, false, false, false),
		F(B5G6R5_UNORM, B5G6R5, UNorm, 3, 0, 2, true, false, false),
		F(B5G5R5A1_UNORM, B5G5R5A1, UNorm, 4, 0, 2, true, false, false),
		F(B8G8R8A8_UNORM, Channels, UNorm, 4, 8, 4, true, false, false),
		F(B8G8R8X8_UNORM, Channels, UNorm, 3, 8, 4, true, false, false),
		F(R10G10B10_XR_BIAS_A2_UNORM, R10G10B10_XR_A2, UNorm, 4, 0, 4, false, false, false),
		F(B8G8R8A8_TYPELESS, Channels, Typeless, 4, 8, 4, true, false, false),
		F(B8G8R8A8_UNORM_SRGB, Channels, Srgb, 4, 8, 4, true, false, false),
		F(B8G8R8X8_TYPELESS, Channels, Typeless, 3, 8, 4, true, false, false),
		F(B8G8R8X8_UNORM_SRGB, Channels, Srgb, 3, 8, 4, true, false, false),
		F(BC6H_TYPELESS, BC6H, Typeless, 3, 0, 16, false, false, false),
		F(BC6H_UF16, BC6H, Float, 3, 0, 16, false, false, false),
		F(BC6H_SF16, BC6H, Float, 3, 0, 16, false, false, false),
		F(BC7_TYPELESS, BC7, Typeless, 4, 0, 16, false, false, false),
		F(BC7_UNORM, BC7, UNorm, 4, 0, 16, false, false, false),
		F(BC7_UNORM_SRGB, BC7, Srgb, 4, 0, 16, false, false, false),
		F(B4G4R4A4_UNORM, B4G4R4A4, UNorm, 4, 0, 2, true, false, false),
	};
#undef F

	bool IsBlock(Codec codec) { return codec >= Codec::BC1 && codec <= Codec::BC7; }

	const FormatInfo* DxgiFormat(DXGI_FORMAT format) {
		for (const FormatInfo& info : kDxgi)
			if (info.format == (u32)format) return &info;
		return nullptr;
	}

	const char* DxgiFormatName(DXGI_FORMAT format) {
		const FormatInfo* info = DxgiFormat(format);
		return info ? info->name : nullptr;
	}

	DXGI_FORMAT DxgiDefaultView(DXGI_FORMAT format) {
		switch (format) {
			case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
			case DXGI_FORMAT_R32G32B32_TYPELESS: return DXGI_FORMAT_R32G32B32_FLOAT;
			case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
			case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
			case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
			case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
			case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
			case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
			case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_D24_UNORM_S8_UINT;
			case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
			case DXGI_FORMAT_R8G8_TYPELESS: return DXGI_FORMAT_R8G8_UNORM;
			case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_FLOAT;
			case DXGI_FORMAT_R8_TYPELESS: return DXGI_FORMAT_R8_UNORM;
			case DXGI_FORMAT_BC1_TYPELESS: return DXGI_FORMAT_BC1_UNORM;
			case DXGI_FORMAT_BC2_TYPELESS: return DXGI_FORMAT_BC2_UNORM;
			case DXGI_FORMAT_BC3_TYPELESS: return DXGI_FORMAT_BC3_UNORM;
			case DXGI_FORMAT_BC4_TYPELESS: return DXGI_FORMAT_BC4_UNORM;
			case DXGI_FORMAT_BC5_TYPELESS: return DXGI_FORMAT_BC5_UNORM;
			case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
			case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
			case DXGI_FORMAT_BC6H_TYPELESS: return DXGI_FORMAT_BC6H_UF16;
			case DXGI_FORMAT_BC7_TYPELESS: return DXGI_FORMAT_BC7_UNORM;
			default: return format;
		}
	}

	DXGI_FORMAT DxgiPlaneFormat(DXGI_FORMAT format, u32 plane) {
		switch (format) {
			case DXGI_FORMAT_R24G8_TYPELESS:
			case DXGI_FORMAT_D24_UNORM_S8_UINT:
			case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
			case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
				return plane == 0 ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : DXGI_FORMAT_R8_UINT;
			case DXGI_FORMAT_R32G8X24_TYPELESS:
			case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
			case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
			case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
				return plane == 0 ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R8_UINT;
			default: return format;
		}
	}

	float HalfToFloat(u16 half) {
		const u32 sign = (u32)(half & 0x8000) << 16;
		const u32 exponent = (half >> 10) & 0x1F;
		u32 mantissa = half & 0x3FF;
		u32 bits;
		if (exponent == 0) {
			if (mantissa == 0) bits = sign;
			else {
				int e = -1;
				do {
					++e;
					mantissa <<= 1;
				} while (!(mantissa & 0x400));
				bits = sign | (u32)(127 - 15 - e) << 23 | (mantissa & 0x3FF) << 13;
			}
		}
		else if (exponent == 31) bits = sign | 0x7F800000 | mantissa << 13;
		else bits = sign | (exponent + 112) << 23 | mantissa << 13;
		float value;
		memcpy(&value, &bits, 4);
		return value;
	}

	u16 FloatToHalf(float value) {
		u32 bits;
		memcpy(&bits, &value, 4);
		const u32 sign = (bits >> 16) & 0x8000;
		const i32 exponent = (i32)((bits >> 23) & 0xFF) - 127 + 15;
		const u32 mantissa = bits & 0x7FFFFF;
		if (((bits >> 23) & 0xFF) == 0xFF) return (u16)(sign | 0x7C00 | (mantissa ? 0x200 : 0));
		if (exponent >= 31) return (u16)(sign | 0x7C00);
		if (exponent <= 0) {
			if (exponent < -10) return (u16)sign;
			const u32 m = (mantissa | 0x800000) >> (1 - exponent);
			return (u16)(sign | ((m + 0x1000) >> 13));
		}
		return (u16)(sign | (u32)exponent << 10 | ((mantissa + 0x1000) >> 13));
	}

	// An unsigned float of `mantissaBits` and a 5-bit exponent (R11G11B10's channels).
	static float SmallFloat(u32 bits, u32 mantissaBits) {
		const u32 exponent = bits >> mantissaBits;
		const u32 mantissa = bits & ((1u << mantissaBits) - 1);
		if (exponent == 0) return mantissa ? std::ldexp((float)mantissa, -14 - (int)mantissaBits) : 0.0f;
		if (exponent == 31) return mantissa ? NAN : INFINITY;
		return std::ldexp(1.0f + (float)mantissa / (float)(1u << mantissaBits), (int)exponent - 15);
	}

	static float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

	static double Channel(const u8* at, u32 bits, Component component) {
		u32 raw32 = 0;
		switch (bits) {
			case 8: raw32 = *at; break;
			case 16: memcpy(&raw32, at, 2); break;
			case 32: memcpy(&raw32, at, 4); break;
		}
		const double maxValue = bits == 32 ? 4294967295.0 : (double)((1ull << bits) - 1);
		switch (component) {
			case C::Float:
				if (bits == 16) return HalfToFloat((u16)raw32);
				if (bits == 32) {
					float f;
					memcpy(&f, &raw32, 4);
					return f;
				}
				return raw32;
			case C::UNorm:
			case C::Typeless:
				return raw32 / maxValue;
			case C::Srgb: return raw32 / maxValue;
			case C::SNorm: {
				const i32 s = bits == 8 ? (i32)(int8_t)raw32 : bits == 16 ? (i32)(int16_t)raw32 : (i32)raw32;
				const double half = (double)((1ull << (bits - 1)) - 1);
				double v = s / half;
				return v < -1.0 ? -1.0 : v;
			}
			case C::UInt: return raw32;
			case C::SInt: return bits == 8 ? (double)(int8_t)raw32 : bits == 16 ? (double)(int16_t)raw32 : (double)(i32)raw32;
			default: return raw32;
		}
	}

	void TexelValues(const FormatInfo& info, const u8* texel, double values[4], u32& count) {
		values[0] = values[1] = values[2] = values[3] = 0;
		count = info.channels;
		u32 packed = 0;
		if (info.bytes >= 4) memcpy(&packed, texel, 4);
		else if (info.bytes == 2) memcpy(&packed, texel, 2);
		switch (info.codec) {
			case Codec::Channels: {
				const u32 step = info.bits / 8;
				for (u32 c = 0; c < info.channels; ++c) values[c] = Channel(texel + c * step, info.bits, info.component);
				if (info.bgr && info.channels >= 3) {
					const double b = values[0];
					values[0] = values[2];
					values[2] = b;
				}
				if (info.format == DXGI_FORMAT_A8_UNORM) {
					values[3] = values[0];
					values[0] = 0;
					count = 4;
				}
				break;
			}
			case Codec::R10G10B10A2:
			case Codec::R10G10B10_XR_A2: {
				const double r = packed & 0x3FF, g = (packed >> 10) & 0x3FF, b = (packed >> 20) & 0x3FF, a = packed >> 30;
				if (info.component == C::UInt) {
					values[0] = r, values[1] = g, values[2] = b, values[3] = a;
				}
				else if (info.codec == Codec::R10G10B10_XR_A2) {
					values[0] = (r - 384) / 510.0, values[1] = (g - 384) / 510.0, values[2] = (b - 384) / 510.0, values[3] = a / 3.0;
				}
				else {
					values[0] = r / 1023.0, values[1] = g / 1023.0, values[2] = b / 1023.0, values[3] = a / 3.0;
				}
				break;
			}
			case Codec::R11G11B10F:
				values[0] = SmallFloat(packed & 0x7FF, 6);
				values[1] = SmallFloat((packed >> 11) & 0x7FF, 6);
				values[2] = SmallFloat(packed >> 22, 5);
				break;
			case Codec::R9G9B9E5: {
				const double scale = std::ldexp(1.0, (int)(packed >> 27) - 15 - 9);
				values[0] = (packed & 0x1FF) * scale;
				values[1] = ((packed >> 9) & 0x1FF) * scale;
				values[2] = ((packed >> 18) & 0x1FF) * scale;
				break;
			}
			case Codec::B5G6R5:
				values[0] = ((packed >> 11) & 0x1F) / 31.0;
				values[1] = ((packed >> 5) & 0x3F) / 63.0;
				values[2] = (packed & 0x1F) / 31.0;
				break;
			case Codec::B5G5R5A1:
				values[0] = ((packed >> 10) & 0x1F) / 31.0;
				values[1] = ((packed >> 5) & 0x1F) / 31.0;
				values[2] = (packed & 0x1F) / 31.0;
				values[3] = (packed >> 15) & 1;
				break;
			case Codec::B4G4R4A4:
				values[0] = ((packed >> 8) & 0xF) / 15.0;
				values[1] = ((packed >> 4) & 0xF) / 15.0;
				values[2] = (packed & 0xF) / 15.0;
				values[3] = ((packed >> 12) & 0xF) / 15.0;
				break;
			case Codec::D24S8:
				values[0] = (packed & 0xFFFFFF) / 16777215.0;
				values[1] = packed >> 24;
				if (!info.depth) {
					values[0] = values[1];
					count = 1;
				}
				break;
			case Codec::D32S8X24: {
				float depth;
				memcpy(&depth, texel, 4);
				values[0] = depth;
				values[1] = texel[4];
				if (!info.depth) {
					values[0] = values[1];
					count = 1;
				}
				break;
			}
			case Codec::R8G8_B8G8:
			case Codec::G8R8_G8B8:
				values[0] = texel[0] / 255.0, values[1] = texel[1] / 255.0, values[2] = texel[2] / 255.0;
				break;
			default:
				count = 0;
				break;
		}
	}

	// --- Block formats ---------------------------------------------------------------------------------------------------

	static void Color565(u16 c, float out[3]) {
		out[0] = ((c >> 11) & 0x1F) / 31.0f;
		out[1] = ((c >> 5) & 0x3F) / 63.0f;
		out[2] = (c & 0x1F) / 31.0f;
	}

	static void DecodeBC1Colors(const u8* block, float out[16][4], bool forceFour) {
		u16 c0, c1;
		memcpy(&c0, block, 2);
		memcpy(&c1, block + 2, 2);
		float palette[4][4];
		Color565(c0, palette[0]);
		Color565(c1, palette[1]);
		palette[0][3] = palette[1][3] = 1.0f;
		if (c0 > c1 || forceFour) {
			for (int i = 0; i < 3; ++i) {
				palette[2][i] = (2 * palette[0][i] + palette[1][i]) / 3.0f;
				palette[3][i] = (palette[0][i] + 2 * palette[1][i]) / 3.0f;
			}
			palette[2][3] = palette[3][3] = 1.0f;
		}
		else {
			for (int i = 0; i < 3; ++i) {
				palette[2][i] = (palette[0][i] + palette[1][i]) / 2.0f;
				palette[3][i] = 0.0f;
			}
			palette[2][3] = 1.0f;
			palette[3][3] = 0.0f;
		}
		u32 indices;
		memcpy(&indices, block + 4, 4);
		for (int p = 0; p < 16; ++p) {
			const u32 index = (indices >> (2 * p)) & 3;
			memcpy(out[p], palette[index], sizeof(float) * 4);
		}
	}

	static void DecodeBC4Channel(const u8* block, float out[16][4], int channel, bool snorm) {
		float palette[8];
		if (snorm) {
			const float r0 = (int8_t)block[0] < -127 ? -1.0f : (int8_t)block[0] / 127.0f;
			const float r1 = (int8_t)block[1] < -127 ? -1.0f : (int8_t)block[1] / 127.0f;
			palette[0] = r0;
			palette[1] = r1;
			if ((int8_t)block[0] > (int8_t)block[1]) {
				for (int i = 1; i < 7; ++i) palette[i + 1] = ((7 - i) * r0 + i * r1) / 7.0f;
			}
			else {
				for (int i = 1; i < 5; ++i) palette[i + 1] = ((5 - i) * r0 + i * r1) / 5.0f;
				palette[6] = -1.0f;
				palette[7] = 1.0f;
			}
		}
		else {
			const float r0 = block[0] / 255.0f, r1 = block[1] / 255.0f;
			palette[0] = r0;
			palette[1] = r1;
			if (block[0] > block[1]) {
				for (int i = 1; i < 7; ++i) palette[i + 1] = ((7 - i) * r0 + i * r1) / 7.0f;
			}
			else {
				for (int i = 1; i < 5; ++i) palette[i + 1] = ((5 - i) * r0 + i * r1) / 5.0f;
				palette[6] = 0.0f;
				palette[7] = 1.0f;
			}
		}
		u64 indices = 0;
		memcpy(&indices, block + 2, 6);
		for (int p = 0; p < 16; ++p) out[p][channel] = palette[(indices >> (3 * p)) & 7];
	}

	static bool DecodeBlock(const FormatInfo& info, const u8* block, float out[16][4]) {
		for (int p = 0; p < 16; ++p) out[p][0] = out[p][1] = out[p][2] = 0, out[p][3] = 1;
		switch (info.codec) {
			case Codec::BC1: DecodeBC1Colors(block, out, false); return true;
			case Codec::BC2:
				DecodeBC1Colors(block + 8, out, true);
				for (int p = 0; p < 16; ++p) out[p][3] = ((block[p / 2] >> (4 * (p & 1))) & 0xF) / 15.0f;
				return true;
			case Codec::BC3:
				DecodeBC1Colors(block + 8, out, true);
				DecodeBC4Channel(block, out, 3, false);
				return true;
			case Codec::BC4: DecodeBC4Channel(block, out, 0, info.component == C::SNorm); return true;
			case Codec::BC5:
				DecodeBC4Channel(block, out, 0, info.component == C::SNorm);
				DecodeBC4Channel(block + 8, out, 1, info.component == C::SNorm);
				return true;
			default: return false;   // BC6H and BC7 are decoded by the replayer on the GPU
		}
	}

	void DecodeRows(const FormatInfo& info, const u8* src, size_t rowPitch, u32 width, u32 height, float* out) {
		if (IsBlock(info.codec)) {
			float block[16][4];
			for (u32 by = 0; by < (height + 3) / 4; ++by) {
				for (u32 bx = 0; bx < (width + 3) / 4; ++bx) {
					const bool known = DecodeBlock(info, src + by * rowPitch + bx * info.bytes, block);
					for (u32 p = 0; p < 16; ++p) {
						const u32 x = bx * 4 + p % 4, y = by * 4 + p / 4;
						if (x >= width || y >= height) continue;
						float* texel = out + ((size_t)y * width + x) * 4;
						if (known) memcpy(texel, block[p], sizeof(float) * 4);
						else texel[0] = texel[1] = texel[2] = 0, texel[3] = 1;
						if (info.component == C::Srgb)
							for (int c = 0; c < 3; ++c) texel[c] = SrgbToLinear(texel[c]);
					}
				}
			}
			return;
		}
		double values[4];
		u32 count = 0;
		for (u32 y = 0; y < height; ++y) {
			const u8* row = src + y * rowPitch;
			for (u32 x = 0; x < width; ++x) {
				float* texel = out + ((size_t)y * width + x) * 4;
				if (info.codec == Codec::R8G8_B8G8 || info.codec == Codec::G8R8_G8B8) {
					const u8* pair = row + (x / 2) * 4;
					const bool rbg = info.codec == Codec::R8G8_B8G8;
					texel[0] = (rbg ? pair[0] : pair[1]) / 255.0f;
					texel[1] = (rbg ? (x & 1 ? pair[3] : pair[1]) : (x & 1 ? pair[2] : pair[0])) / 255.0f;
					texel[2] = (rbg ? pair[2] : pair[3]) / 255.0f;
					texel[3] = 1.0f;
					continue;
				}
				TexelValues(info, row + (size_t)x * info.bytes, values, count);
				texel[0] = (float)values[0];
				texel[1] = count > 1 ? (float)values[1] : 0.0f;
				texel[2] = count > 2 ? (float)values[2] : 0.0f;
				texel[3] = count > 3 ? (float)values[3] : 1.0f;
				if (info.format == DXGI_FORMAT_A8_UNORM) texel[3] = (float)values[3];
				if (info.depth && info.stencil && info.codec != Codec::Channels) texel[1] = (float)values[1];
				if (info.component == C::Srgb)
					for (int c = 0; c < 3; ++c) texel[c] = SrgbToLinear(texel[c]);
			}
		}
	}
}
