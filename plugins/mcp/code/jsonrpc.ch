// JSON-RPC 2.0 as MCP speaks it (docs/04 "MCP" Protocol; the 2025-06-18 specification): the messages IED writes -
// requests, notifications, answers to a server's requests - and the ones it reads back, one message a line on stdio (a
// message never holds a raw line break) or a body or an event's data on streamable HTTP.

include "api/plugin.ch"

namespace Mcp
{
	const char* kProtocolVersion = "2025-06-18";
	const int64 kMethodNotFound = -32601;

	enum class MessageKind { Request, Notification, Response, Invalid }

	class Message
	{
		public MessageKind kind;
		public Core::String id;                  // the id as JSON text ("7", "\"a1\""); empty for a notification
		public int64 number;                     // a whole-number id's value, else -1
		public Core::String method;
		public Serialization::DomNode params;
		public Serialization::DomNode result;
		public Serialization::DomNode error;     // {code, message, data?}
		public Serialization::DomDoc* doc;
		public Core::String text;                // as it was read
	}

	// The messages `text` holds: one JSON-RPC 2.0 object, or a batch of them (an older server's); a message that is not
	// one is Invalid with its text, so the caller can say what came.
	Core::Array<Message*> ReadMessages(Core::StringView text);

	Core::String RequestText(int64 id, Core::StringView method, Core::StringView paramsJson);
	// `paramsJson` empty: no params.
	Core::String NotificationText(Core::StringView method, Core::StringView paramsJson);
	Core::String ResultText(Core::StringView idJson, Core::StringView resultJson);
	Core::String ErrorText(Core::StringView idJson, int64 code, Core::StringView message);
	// An error object as "<message> (code <n>)".
	Core::String Describe(const Serialization::DomNode& error);
	// The initialize request's params: this protocol version, no client capabilities, IED as the client.
	Core::String InitializeParams(Core::StringView clientVersion);
	// Whether a server's protocolVersion is one IED speaks (2025-06-18, 2025-03-26, 2024-11-05: the same messages).
	bool KnownVersion(Core::StringView version);

	// Lines of a byte stream that arrives in pieces: "\n" ends one, a "\r" before it is dropped, blank lines are skipped.
	class LineSplitter
	{
		public void Feed(Core::StringView bytes);
		public bool Next(Core::String& line);
		// What came after the last line break (a server that ended mid-line).
		public Core::String Rest();

		private Core::String pending;
	}
}
