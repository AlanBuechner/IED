// The client side of the control channel (docs/10 "Local control channel"): one JSON-RPC 2.0 request per line to the
// user's ied-server.

include "bedrock.ch"

namespace Launch
{
	const int32 kConnectMs = 2000;
	const int32 kAnswerMs = 30000;

	// A response: its result as JSON text, or why there is none.
	class Reply
	{
		public bool ok;
		public Core::String result;
		public Core::String error;
	}

	// The pipe `control` names, or this user's server's when it is empty.
	Core::String ControlName(Core::StringView control);
	// A JSON-RPC 2.0 request line.
	Core::String RequestLine(Core::StringView method, Core::StringView paramsJson);
	// A response line read into a Reply.
	Reply ParseReply(Core::StringView line);
	// One string member of a reply's result; empty when it has none.
	Core::String ResultField(const Reply& reply, const char* name);

	// One request on an open connection.
	Reply Ask(Net::LocalStream& stream, Core::StringView method, Core::StringView paramsJson);
}
