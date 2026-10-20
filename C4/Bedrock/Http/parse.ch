// Reading HTTP/1.1 off the wire (D-66, RFC 9112): message heads, how a body is framed, and chunked bodies. Pure
// functions over bytes - they never touch a socket - so the server and the client share them and tests feed them
// any bytes at all.

include "message.ch"

namespace Http
{
	enum class ParseStatus { Complete, Incomplete, Malformed, TooLarge }

	// How far a head got: Complete with its length (through the empty line), Incomplete until the empty line arrives,
	// TooLarge past the limit, Malformed with why.
	struct HeadParse
	{
		public ParseStatus status;
		public int64 length;
		public Core::String error;
		public int32 answer;   // what a server answers a head that failed with: 400, 431, 505
	}

	// The request line and headers at the start of `bytes`, into `request`. Leading empty lines are skipped; lines may
	// end in CRLF or LF. A version other than HTTP/1.x is Malformed.
	HeadParse ParseRequestHead(Core::StringView bytes, int64 maxBytes, Request& request);
	// The status line and headers at the start of `bytes`, into `response`.
	HeadParse ParseResponseHead(Core::StringView bytes, int64 maxBytes, Response& response);

	enum class BodyKind { None, Length, Chunked, UntilClose, Malformed, Unsupported }

	// How the body after a head is framed: `length` bytes, chunked, none, or (a response with neither header) until
	// the connection closes. Malformed for a bad or conflicting Content-Length, or both it and Transfer-Encoding;
	// Unsupported for a transfer coding other than chunked.
	struct BodyFraming
	{
		public BodyKind kind;
		public int64 length;
		public Core::String error;
	}

	BodyFraming RequestFraming(const Headers& headers);
	// `status` and the request's method decide too: HEAD, 1xx, 204 and 304 responses have no body.
	BodyFraming ResponseFraming(const Headers& headers, int32 status, Core::StringView requestMethod);

	// What one Feed did: how many bytes it consumed, and Complete once the last chunk and trailers are in.
	struct ChunkedStep
	{
		public ParseStatus status;
		public int64 consumed;
		public Core::String error;
	}

	// Where a ChunkedDecoder is: expecting a chunk-size line, inside a chunk's data, expecting the line break after
	// the data, reading trailer lines after the last chunk, or done.
	enum class ChunkStage { Size, Data, DataEnd, Trailer, Done }

	// A chunked body read as it arrives: feed it what has come (from where the last Feed stopped), and it appends the
	// data to the body. Chunk extensions and trailers are read and dropped.
	class ChunkedDecoder
	{
		public ChunkedStep Feed(Core::StringView bytes, int64 maxBody, Core::Array<uint8>& body);
		public bool Done() const { return stage == ChunkStage::Done; }

		// One line, read by the stage: Complete when it was fine, else Malformed or TooLarge with why.
		private ParseStatus TakeLine(Core::StringView line, int64 bodyBytes, int64 maxBody, Core::String& why);

		private ChunkStage stage;
		private int64 remaining;
	}

	// Where the line starting at `from` ends (the index of its '\n'), or -1 when it has not arrived.
	int64 LineEnd(Core::StringView bytes, int64 from);
}
