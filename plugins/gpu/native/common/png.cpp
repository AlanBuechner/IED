#include "png.h"

#include <windows.h>

namespace fb
{
	u32 Crc32(const u8* data, size_t size, u32 crc) {
		static u32 table[256];
		static bool ready = false;
		if (!ready) {
			for (u32 i = 0; i < 256; ++i) {
				u32 c = i;
				for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
				table[i] = c;
			}
			ready = true;
		}
		crc = ~crc;
		for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
		return ~crc;
	}

	static void PutBE(std::vector<u8>& out, u32 value) {
		out.push_back((u8)(value >> 24));
		out.push_back((u8)(value >> 16));
		out.push_back((u8)(value >> 8));
		out.push_back((u8)value);
	}

	static void Chunk(std::vector<u8>& out, const char* type, const std::vector<u8>& data) {
		PutBE(out, (u32)data.size());
		const size_t start = out.size();
		out.insert(out.end(), type, type + 4);
		out.insert(out.end(), data.begin(), data.end());
		PutBE(out, Crc32(out.data() + start, out.size() - start));
	}

	std::vector<u8> EncodePng(u32 width, u32 height, const u8* rgba, size_t rowPitch) {
		std::vector<u8> raw;
		raw.reserve((size_t)(width * 4 + 1) * height);
		for (u32 y = 0; y < height; ++y) {
			raw.push_back(0);
			const u8* row = rgba + rowPitch * y;
			raw.insert(raw.end(), row, row + (size_t)width * 4);
		}
		std::vector<u8> zlib = {0x78, 0x01};
		size_t at = 0;
		do {
			const size_t block = raw.size() - at > 65535 ? 65535 : raw.size() - at;
			const bool last = at + block == raw.size();
			zlib.push_back(last ? 1 : 0);
			zlib.push_back((u8)block);
			zlib.push_back((u8)(block >> 8));
			zlib.push_back((u8)~block);
			zlib.push_back((u8)(~block >> 8));
			zlib.insert(zlib.end(), raw.begin() + at, raw.begin() + at + block);
			at += block;
		} while (at < raw.size());
		u32 a = 1, b = 0;
		for (u8 byte : raw) {
			a = (a + byte) % 65521;
			b = (b + a) % 65521;
		}
		PutBE(zlib, (b << 16) | a);
		std::vector<u8> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
		std::vector<u8> ihdr;
		PutBE(ihdr, width);
		PutBE(ihdr, height);
		ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
		Chunk(png, "IHDR", ihdr);
		Chunk(png, "IDAT", zlib);
		Chunk(png, "IEND", {});
		return png;
	}

	bool WritePng(const std::wstring& path, u32 width, u32 height, const u8* rgba, size_t rowPitch) {
		std::vector<u8> png = EncodePng(width, height, rgba, rowPitch);
		HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;
		DWORD wrote = 0;
		const BOOL ok = WriteFile(file, png.data(), (DWORD)png.size(), &wrote, nullptr);
		CloseHandle(file);
		return ok && wrote == png.size();
	}
}
