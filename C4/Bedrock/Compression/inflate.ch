// DEFLATE (RFC 1951) decompression: Compression::Inflate appends a raw stream's data to an array and says how much input
// the stream took, or why it is not one. zlib.ch reads the zlib and gzip wrappers around it.
//     Core::Array<uint8> data;
//     Compression::InflateResult read = Compression::Inflate(stream, streamBytes, data);
//     if (!read.ok) printf("%s\n", read.error);

include "Container/array.ch"

namespace Compression
{
	// What decompressing a stream came to: `ok`, the input bytes it took, and why it failed when it did.
	struct InflateResult
	{
		public bool ok;
		public int64 consumed;
		public const char* error;
	}

	// The data of a raw DEFLATE stream (RFC 1951) at `data`, appended to `out`.
	InflateResult Inflate(const uint8* data, int64 size, Core::Array<uint8>& out);
}
