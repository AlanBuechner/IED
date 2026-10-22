// The zlib (RFC 1950) and gzip (RFC 1952) wrappers of a DEFLATE stream, written and read with their checksums: Adler-32
// for zlib, CRC-32 and the size for gzip.
//     Core::Array<uint8> member;
//     Compression::GzipCompress(bytes, count, Compression::kDefaultLevel, member);
//     Core::Array<uint8> back;
//     bool same = Compression::GzipDecompress(member.Data(), member.Count(), back).ok;

include "inflate.ch"
include "deflate.ch"
include "Container/crc32.ch"
include "Platform/system.ch"

namespace Compression
{
	// The Adler-32 checksum (RFC 1950) of `count` bytes: zlib's.
	uint32 Adler32(const uint8* data, int64 count);

	// One run of at most 5552 bytes added to Adler-32's sums (before the modulus, which the caller takes): sixteen bytes
	// a step on any processor, thirty-two with AVX2, which Adler32 uses where the processor has it. Both give the same
	// sums.
	void AddAdlerRun(const uint8* data, int64 run, uint64& a, uint64& b);
	[target("avx2")] void AddAdlerRunWide(const uint8* data, int64 run, uint64& a, uint64& b);

	// `size` bytes at `data` as a zlib stream (RFC 1950) at `level` (deflate.ch), appended to `out`.
	void ZlibCompress(const uint8* data, int64 size, int32 level, Core::Array<uint8>& out);
	// `size` bytes at `data` as one gzip member (RFC 1952) at `level`, with no name and no time, appended to `out`.
	void GzipCompress(const uint8* data, int64 size, int32 level, Core::Array<uint8>& out);

	// The data of a zlib stream (RFC 1950): its header, a DEFLATE stream and the Adler-32 of the data, which must match.
	InflateResult ZlibDecompress(const uint8* data, int64 size, Core::Array<uint8>& out);
	// The data of a gzip member (RFC 1952): its header (with its optional fields), a DEFLATE stream, and the CRC-32 and
	// size of the data, which must match. `consumed` says where a next member would start.
	InflateResult GzipDecompress(const uint8* data, int64 size, Core::Array<uint8>& out);
}
