// DEFLATE (RFC 1951) and its gzip wrapper (RFC 1952), decoding only: pprof profiles arrive gzip-compressed.

include "bedrock.ch"

namespace Profiler
{
	// The bytes a raw DEFLATE stream decodes to; false with `problem` when it is malformed or would pass `mostBytes`.
	bool Inflate(const uint8* data, int64 length, int64 mostBytes, Core::Array<uint8>& out, Core::String& problem);

	bool IsGzip(const uint8* data, int64 length);
	// A gzip member's contents, its CRC-32 checked.
	bool Gunzip(const uint8* data, int64 length, int64 mostBytes, Core::Array<uint8>& out, Core::String& problem);
}
