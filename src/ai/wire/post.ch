// A POST whose response streams (a model server's SSE, docs/12): send it, read the head, then hand out the body as it
// arrives - chunked, sized, or until the server closes - each read waiting at most the time given, so the caller's
// watchdogs decide what a silence means. Plain http (local model servers); one request per connection.
//     StreamingPost post;
//     if (!post.Send(url, headers, body, 5000, error)) ...
//     post.ReadHead(250) ... post.Read(text, 250)

include "bedrock.ch"

namespace Ied
{
	enum class PostRead { Bytes, Waiting, Ended, Failed }

	class StreamingPost
	{
		[operator("create")] public void Init() {}
		[operator("delete")] public void Delete() { Close(); }

		// Connects to `url` and sends `body` (JSON) with `headers` ("Name: value"); false with `error`.
		public bool Send(Core::StringView url, const Core::Array<Core::String>& headers, Core::StringView body, int32 connectMs, Core::String& error);
		// The response head, waiting at most `waitMs`: Bytes once it is in (Status, Head), Waiting, or Failed (Error).
		public PostRead ReadHead(int32 waitMs);
		// Body bytes appended to `out`, waiting at most `waitMs`: Bytes, Waiting, Ended (the whole body is in) or Failed.
		public PostRead Read(Core::String& out, int32 waitMs);
		public int32 Status() const { return head.status; }
		public const Http::Response& Head() const { return head; }
		public Core::StringView Error() const { return error.View(); }
		public void Close() { stream.Close(); }

		private PostRead Receive(int32 waitMs);
		private PostRead Decode(Core::String& out);

		private Net::TcpStream stream;
		private Http::Response head;
		private bool headIn;
		private Http::BodyFraming framing;
		private Http::ChunkedDecoder chunks;
		private Core::String raw;        // bytes received and not yet decoded
		private int64 bodyRead;
		private bool closed;
		private Core::String error;
	}
}
