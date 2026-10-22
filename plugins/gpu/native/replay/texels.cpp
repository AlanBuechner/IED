#include "texels.h"
#include "png.h"

#include <algorithm>
#include <cmath>

namespace fb::replay
{
	void ShowTexels(const FormatInfo& info, const u8* data, size_t rowPitch, u32 width, u32 height, u32 maxSize, JsonWriter& out, std::vector<u8>& binary) {
		std::vector<float> texels((size_t)width * height * 4);
		DecodeRows(info, data, rowPitch, width, height, texels.data());
		maxSize = std::max<u32>(1, maxSize);
		const u32 step = std::max<u32>(1, (std::max(width, height) + maxSize - 1) / maxSize);
		const u32 outWidth = (width + step - 1) / step, outHeight = (height + step - 1) / step;
		std::vector<float> shown((size_t)outWidth * outHeight * 4);
		double minimum[4] = {INFINITY, INFINITY, INFINITY, INFINITY}, maximum[4] = {-INFINITY, -INFINITY, -INFINITY, -INFINITY};
		double sum[4] = {};
		u64 finite[4] = {};
		u64 nans = 0, infs = 0, negatives = 0;
		size_t firstBad = SIZE_MAX;
		for (size_t i = 0; i < texels.size(); ++i) {
			const float v = texels[i];
			if (std::isnan(v) || std::isinf(v)) {
				++(std::isnan(v) ? nans : infs);
				firstBad = std::min(firstBad, i / 4);
				continue;
			}
			if (v < 0) ++negatives;
			minimum[i % 4] = std::min<double>(minimum[i % 4], v);
			maximum[i % 4] = std::max<double>(maximum[i % 4], v);
			sum[i % 4] += v;
			++finite[i % 4];
		}
		for (u32 y = 0; y < outHeight; ++y) {
			for (u32 x = 0; x < outWidth; ++x) {
				// A box over the step x step texels; a NaN or Inf in the box stays visible.
				float sum[4] = {};
				float special[4] = {};
				bool hasSpecial[4] = {};
				u32 count = 0;
				for (u32 dy = 0; dy < step && y * step + dy < height; ++dy) {
					for (u32 dx = 0; dx < step && x * step + dx < width; ++dx) {
						const float* t = &texels[(((size_t)(y * step + dy)) * width + (x * step + dx)) * 4];
						for (int c = 0; c < 4; ++c) {
							if (std::isnan(t[c]) || std::isinf(t[c])) {
								special[c] = t[c];
								hasSpecial[c] = true;
							}
							else sum[c] += t[c];
						}
						++count;
					}
				}
				float* o = &shown[((size_t)y * outWidth + x) * 4];
				for (int c = 0; c < 4; ++c) o[c] = hasSpecial[c] ? special[c] : sum[c] / (float)std::max<u32>(count, 1);
			}
		}
		binary.resize(shown.size() * sizeof(float));
		memcpy(binary.data(), shown.data(), binary.size());
		out.Uint("width", outWidth);
		out.Uint("height", outHeight);
		out.Uint("srcWidth", width);
		out.Uint("srcHeight", height);
		out.Uint("step", step);
		out.BeginArray("min");
		for (double v : minimum) out.Double(std::isinf(v) ? 0.0 : v);
		out.EndArray();
		out.BeginArray("max");
		for (double v : maximum) out.Double(std::isinf(v) ? 0.0 : v);
		out.EndArray();
		out.BeginArray("mean");
		for (int c = 0; c < 4; ++c) out.Double(finite[c] ? sum[c] / (double)finite[c] : 0.0);
		out.EndArray();
		out.Uint("nan", nans);
		out.Uint("inf", infs);
		out.Uint("negative", negatives);
		if (firstBad != SIZE_MAX) out.BeginObject("firstBad").Uint("x", firstBad % width).Uint("y", firstBad / width).EndObject();
		out.String("encoding", "rgba32f");
	}

	bool SaveShownPng(const JsonValue& shown, const std::vector<u8>& rgba, double lo, double hi, const std::wstring& path) {
		const u32 width = (u32)shown["width"].Uint(), height = (u32)shown["height"].Uint();
		if (!width || !height || rgba.size() < (size_t)width * height * 16) return false;
		const float* texels = (const float*)rgba.data();
		const bool encode = shown["linear"].Bool();
		const double span = hi > lo ? hi - lo : 1.0;
		std::vector<u8> bytes((size_t)width * height * 4);
		for (size_t i = 0; i < (size_t)width * height; ++i) {
			for (int c = 0; c < 3; ++c) {
				const float raw = texels[i * 4 + c];
				double v = std::isnan(raw) ? 0.0 : std::min(std::max((raw - lo) / span, 0.0), 1.0);
				if (encode) v = v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - 0.055;
				bytes[i * 4 + c] = (u8)std::lround(v * 255.0);
			}
			bytes[i * 4 + 3] = 255;
		}
		return WritePng(path, width, height, bytes.data(), (size_t)width * 4);
	}

	void PixelValues(const FormatInfo& info, const u8* data, size_t rowPitch, u32 x, u32 y, JsonWriter& out) {
		if (IsBlock(info.codec)) {
			float block[16 * 4];
			const u8* at = data + (y / 4) * rowPitch + (x / 4) * info.bytes;
			DecodeRows(info, at, rowPitch, 4, 4, block);
			const float* t = &block[((y % 4) * 4 + (x % 4)) * 4];
			out.BeginArray("values");
			for (int c = 0; c < 4; ++c) out.Double(t[c]);
			out.EndArray();
			out.String("text", Format("%.6g, %.6g, %.6g, %.6g", t[0], t[1], t[2], t[3]));
			return;
		}
		const u8* texel = data + (size_t)y * rowPitch + (size_t)x * info.bytes;
		double values[4];
		u32 count = 0;
		TexelValues(info, texel, values, count);
		std::string raw;
		for (u32 i = 0; i < info.bytes; ++i) raw += Format("%02X", texel[info.bytes - 1 - i]);
		out.String("raw", raw);
		out.BeginArray("values");
		for (u32 c = 0; c < count; ++c) out.Double(values[c]);
		out.EndArray();
		std::string text;
		const bool integer = info.component == Component::UInt || info.component == Component::SInt;
		for (u32 c = 0; c < count; ++c) {
			if (c) text += ", ";
			text += integer ? Format("%.0f", values[c]) : Format("%.6g", values[c]);
		}
		out.String("text", text);
	}
}
