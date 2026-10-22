// Content codings (RFC 9110 section 8.4): a response's body gzip-compressed for a client that accepts it, and a body
// that came compressed decoded.
//     Http::CompressResponse(request, response, Compression::kFastestLevel);   // once the handler set the body
//     Core::String why;
//     if (!Http::DecodeBody(got.response.headers, got.response.body, why)) printf("%s\n", why.CStr());

include "message.ch"
include "Compression/zlib.ch"

namespace Http
{
	// The smallest body worth compressing: below it, the gzip member's 18 bytes and the coding's headers cost more.
	const int64 kCompressFromBytes = 1024;

	// Whether an Accept-Encoding value accepts `coding`: it, or `*`, is listed without a q of 0. False for null.
	bool AcceptsCoding(const Core::String* accepted, const char* coding);
	// Whether a Content-Type names data that compresses: text, JSON, JavaScript, XML, SVG, WebAssembly. False for null.
	bool CompressibleType(const Core::String* contentType);

	// The body gzip-compressed at `level` when the request accepts gzip and the body is kCompressFromBytes or more, of a
	// compressible type, with no coding yet, and smaller compressed: Content-Encoding and Vary then say so. True when it
	// compressed.
	bool CompressResponse(const Request& request, Response& response, int32 level);

	// A body whose Content-Encoding is gzip or deflate decoded in place and the header removed; true also when it had
	// no coding. False, with `why`, for another coding or a damaged body, which then stays as it came.
	bool DecodeBody(Headers& headers, Core::Array<uint8>& body, Core::String& why);
}
