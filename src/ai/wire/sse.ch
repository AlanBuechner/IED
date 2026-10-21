// Server-sent events (the stream a model server answers a streaming request with): lines, a blank line ending an event,
// "data:" lines joined by newlines, ":" comments kept apart (a server's keepalive during a long prefill counts as life).
//     SseReader sse;
//     sse.Feed(bytes);
//     SseEvent event;
//     while (sse.Next(event)) ...

include "bedrock.ch"

namespace Ied
{
	enum class SseKind { Data, Comment }

	struct SseEvent
	{
		public SseKind kind;
		public Core::String data;
	}

	class SseReader
	{
		[operator("create")] public void Init() {}

		public void Feed(Core::StringView bytes);
		// The next whole event or comment: true with `event`; false until more bytes arrive.
		public bool Next(SseEvent& event);
		// What a stream that ended mid-event left: its data, if any (a server that closes without the blank line).
		public bool Flush(SseEvent& event);

		private bool Line(Core::StringView line, SseEvent& event);

		private Core::String pending;
		private Core::String data;
		private bool hasData;
	}
}
