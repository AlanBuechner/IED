#include "Model/Types/NameGuid.h"
#include <vector>

namespace Scripting::NameGuid
{
	namespace
	{
		// 6ba7b811-9dad-11d1-80b4-00c04fd430c8, in network byte order.
		constexpr uint8_t kUrlNamespace[16] = { 0x6b, 0xa7, 0xb8, 0x11, 0x9d, 0xad, 0x11, 0xd1,
			0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8 };
		constexpr uint8_t kVersion5 = 0x50;
		constexpr uint8_t kRfc4122Variant = 0x80;

		uint32_t RotateLeft(uint32_t value, int bits) { return (value << bits) | (value >> (32 - bits)); }

		void Sha1Block(uint32_t state[5], const uint8_t* block)
		{
			uint32_t w[80];
			for (int i = 0; i < 16; ++i)
				w[i] = uint32_t(block[i * 4]) << 24 | uint32_t(block[i * 4 + 1]) << 16 | uint32_t(block[i * 4 + 2]) << 8 | block[i * 4 + 3];
			for (int i = 16; i < 80; ++i) w[i] = RotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
			uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
			for (int i = 0; i < 80; ++i)
			{
				uint32_t f, k;
				if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
				else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
				else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
				else { f = b ^ c ^ d; k = 0xca62c1d6; }
				const uint32_t next = RotateLeft(a, 5) + f + e + k + w[i];
				e = d; d = c; c = RotateLeft(b, 30); b = a; a = next;
			}
			state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
		}

		std::array<uint8_t, 20> Sha1(std::vector<uint8_t> message)
		{
			const uint64_t bitLength = uint64_t(message.size()) * 8;
			message.push_back(0x80);
			while (message.size() % 64 != 56) message.push_back(0);
			for (int i = 7; i >= 0; --i) message.push_back(uint8_t(bitLength >> (i * 8)));
			uint32_t state[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
			for (size_t at = 0; at < message.size(); at += 64) Sha1Block(state, &message[at]);
			std::array<uint8_t, 20> digest{};
			for (int i = 0; i < 20; ++i) digest[i] = uint8_t(state[i / 4] >> (24 - 8 * (i % 4)));
			return digest;
		}
	}

	std::array<uint8_t, 16> GuidBytes(const std::string& name)
	{
		std::vector<uint8_t> message(kUrlNamespace, kUrlNamespace + 16);
		message.insert(message.end(), name.begin(), name.end());
		const std::array<uint8_t, 20> digest = Sha1(std::move(message));
		uint8_t uuid[16];
		for (int i = 0; i < 16; ++i) uuid[i] = digest[i];
		uuid[6] = uint8_t((uuid[6] & 0x0f) | kVersion5);
		uuid[8] = uint8_t((uuid[8] & 0x3f) | kRfc4122Variant);
		// A _GUID stores Data1, Data2 and Data3 little-endian; the UUID has them big-endian.
		return { uuid[3], uuid[2], uuid[1], uuid[0], uuid[5], uuid[4], uuid[7], uuid[6],
			uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15] };
	}
}
