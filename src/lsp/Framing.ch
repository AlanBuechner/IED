// LSP's base protocol (docs/24): each message is a header ("Content-Length: <n>", any other header lines ignored), a
// blank line, then <n> bytes of JSON.
//     FrameReader reader;
//     reader.Feed(bytes);                       // as they arrive from the server
//     Core::String body;
//     while (reader.Next(body)) Handle(body);

include "bedrock.ch"

namespace Ied
{
	const int64 c_LspMaxMessageBytes = 64 * 1024 * 1024;

	class FrameReader
	{
		[operator("create")] public void Init() {}

		public void Feed(Core::StringView bytes);
		// The next whole message's body: true with `body`; false until more bytes arrive. A header without a valid
		// Content-Length makes Failed() true, and nothing after it can be read.
		public bool Next(Core::String& body);
		public bool Failed() const { return !m_Failure.IsEmpty(); }
		public Core::StringView Failure() const { return m_Failure.View(); }

		private Core::String m_Pending;
		private Core::String m_Failure;
	}

	// One message's body framed for writing.
	Core::String FrameMessage(Core::StringView body);
	// The Content-Length of a header block (the text before the blank line), or -1 when it has none or a bad one.
	int64 ContentLengthOf(Core::StringView header);
}
