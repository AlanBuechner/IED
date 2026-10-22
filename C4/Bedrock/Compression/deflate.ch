// DEFLATE (RFC 1951) compression at zlib's levels: each block is stored, coded with the fixed codes or with codes of its
// own, whichever is smallest. zlib.ch wraps the stream for zlib and gzip.
//     Core::Array<uint8> stream;
//     Compression::Deflate(bytes, count, Compression::kDefaultLevel, stream);

include "Container/array.ch"

namespace Compression
{
	// How hard Deflate looks for repeats: 0 stores the data, 1 is fastest, 9 compresses most; 6 is zlib's default.
	const int32 kStoreLevel = 0;
	const int32 kFastestLevel = 1;
	const int32 kDefaultLevel = 6;
	const int32 kBestLevel = 9;

	// `size` bytes at `data` as a raw DEFLATE stream (RFC 1951), appended to `out`.
	void Deflate(const uint8* data, int64 size, int32 level, Core::Array<uint8>& out);

	// The match finders' positions moved `shift` lower, those at or below it gone: what keeps 32-bit positions valid
	// past 4 GiB of input.
	void SlideEntries(uint32* entries, int64 count, uint32 shift);
}
