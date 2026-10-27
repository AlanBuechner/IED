// One HTTP exchange with a streamable-HTTP MCP server (docs/04 "MCP" Protocol): a POST of a JSON-RPC message (or the
// DELETE that ends a session) with its headers, then the answer's head and its body as it comes - chunked, sized, or
// until the server closes - each read waiting at most the time given, so the caller can look at its cancel flag between
// reads. Plain http: Bedrock's client has no TLS. And the event stream such an answer may be: "data:" lines joined, a
// blank line ending an event.

include "bedrock.ch"

namespace Mcp
{
	enum class BodyRead { Bytes, Waiting, Ended, Failed }

	class HttpExchange
	{
		[operator("create")] public void Init() {}
		[operator("delete")] public void Delete() { Close(); }

		// Connects to `url` and sends `body` with `headers` ("Name: value"); false with `error`.
		public bool Send(Core::StringView method, Core::StringView url, const Core::Array<Core::String>& headers, Core::StringView body, int32 connectMs, Core::String& error);
		// The answer's head: Bytes once it is in, Waiting, or Failed (Error).
		public BodyRead ReadHead(int32 waitMs);
		// Body bytes appended to `out`: Bytes, Waiting, Ended (the whole body is in) or Failed.
		public BodyRead Read(Core::String& out, int32 waitMs);
		public int32 Status() const { return head.status; }
		// A header of the answer ("" when it has none).
		public Core::String Header(Core::StringView name) const;
		public Core::StringView Error() const { return error.View(); }
		public void Close() { stream.Close(); }

		private BodyRead Receive(int32 waitMs);
		private BodyRead Decode(Core::String& out);

		private Core::String method;
		private Net::TcpStream stream;
		private Http::Response head;
		private bool headIn;
		private Http::BodyFraming framing;
		private Http::ChunkedDecoder chunks;
		private Core::String raw;
		private int64 bodyRead;
		private bool closed;
		private Core::String error;
	}

	// Server-sent events: the data of each whole event in turn.
	class EventReader
	{
		public void Feed(Core::StringView bytes);
		// The next whole event's data (its "data:" lines joined with line breaks); false until more bytes come.
		public bool Next(Core::String& data);
		// What a stream that ended mid-event left.
		public bool Flush(Core::String& data);

		private bool Line(Core::StringView line, Core::String& done);

		private Core::String pending;
		private Core::String data;
		private bool hasData;
	}
}
