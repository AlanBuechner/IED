// LZ4 frames (lz4.ch): the block format inside lz4's frame, with its descriptor, block sizes and XXH32 checksums.

include "inflate.ch"
include "xxhash.ch"

namespace Compression
{
	// `size` bytes as one LZ4 frame, appended to `out`: independent blocks of up to 4 MiB, the content size and the
	// content's XXH32. A block that would not shrink is stored as it is.
	void Lz4CompressFrame(const uint8* data, int64 size, Core::Array<uint8>& out, int32 acceleration = 1);

	// The data of the LZ4 frame at `data` (any block size, independent or linked blocks, block and content checksums,
	// the content size, skippable frames before it), appended to `out`. `consumed` says where a next frame would start.
	InflateResult Lz4DecompressFrame(const uint8* data, int64 size, Core::Array<uint8>& out);
}
