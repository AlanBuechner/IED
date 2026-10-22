// LZ4 (lz4 1.9's block and frame formats): byte-oriented LZ77 with no entropy stage, so it decodes at memory speed and
// compresses fast, which is what streamed game assets want; DEFLATE (deflate.ch) packs tighter.
//     Core::Array<uint8> frame;
//     Compression::Lz4CompressFrame(bytes, count, frame);
//     Core::Array<uint8> back;
//     bool same = Compression::Lz4DecompressFrame(frame.Data(), frame.Count(), back).ok;

include "inflate.ch"
include "lz4frame.ch"

namespace Compression
{
	// The most bytes one block of `size` input bytes can take.
	int64 Lz4Bound(int64 size);

	// `size` bytes at `data` as one LZ4 block, appended to `out`. `acceleration` (1 and up, as lz4's) skips ahead faster
	// through data that does not match: quicker, larger.
	void Lz4CompressBlock(const uint8* data, int64 size, Core::Array<uint8>& out, int32 acceleration = 1);

	// The data of the LZ4 block at `data`, appended to `out`, at most `limit` bytes of it. A match may reach back into
	// the `history` bytes of `out` before what this block adds (0: none; a frame's linked blocks give the earlier ones).
	InflateResult Lz4DecompressBlock(const uint8* data, int64 size, int64 limit, Core::Array<uint8>& out, int64 history = 0);
}
