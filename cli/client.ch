// The CLI's side of the control channel (docs/10): one JSON-RPC 2.0 request per line to the user's ied-server, and
// starting that server when none runs.

include "bedrock.ch"

namespace Cli
{
	const int32 kConnectMs = 2000;
	const int32 kAnswerMs = 30000;
	const int64 kServerStartMs = 15000;

	// A response: its result as JSON text, or why there is none.
	class Reply
	{
		public bool ok;
		public Core::String result;
		public Core::String error;
	}

	// A JSON-RPC 2.0 request line.
	Core::String RequestLine(Core::StringView method, Core::StringView paramsJson);
	// A response line read into a Reply.
	Reply ParseReply(Core::StringView line);

	// One request on an open connection.
	Reply Ask(Net::LocalStream& stream, Core::StringView method, Core::StringView paramsJson);
	// The server's ied-server.exe beside this program or in a sibling folder (the build layout); empty when none is.
	FS::Path ServerProgram();
	// The web client's folder: the nearest "frontend" above this program that holds login.html; empty when none does.
	FS::Path WebClientFolder();
	// Starts the server in the background, then waits for its pipe: a connection, or a closed stream with `error`.
	Net::LocalStream StartServer(Core::StringView control, Core::String& error);
	// Waits (at most kServerStartMs) until no one listens on `control`: false when someone still does.
	bool AwaitGone(Core::StringView control);
}
