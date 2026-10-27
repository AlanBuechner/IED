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
		public int32 Status() const { return m_Head.status; }
		// A header of the answer ("" when it has none).
		public Core::String Header(Core::StringView name) const;
		public Core::StringView Error() const { return m_Error.View(); }
		public void Close() { m_Stream.Close(); }

		private BodyRead Receive(int32 waitMs);
		private BodyRead Decode(Core::String& out);

		private Core::String m_Method;
		private Net::TcpStream m_Stream;
		private Http::Response m_Head;
		private bool m_HeadIn;
		private Http::BodyFraming m_Framing;
		private Http::ChunkedDecoder m_Chunks;
		private Core::String m_Raw;
		private int64 m_BodyRead;
		private bool m_Closed;
		private Core::String m_Error;
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

		private Core::String m_Pending;
		private Core::String m_Data;
		private bool m_HasData;
	}
}
