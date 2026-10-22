// XXH32 (Yann Collet's xxHash, 32-bit): the checksum of LZ4 frames. Fast and well spread, not for secrets.

namespace Compression
{
	uint32 Xxh32(const uint8* data, int64 size, uint32 seed = 0u);
}
