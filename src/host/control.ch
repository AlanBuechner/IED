// Control (docs/10 "Local control channel"): the pipe the `ied` CLI and second launches talk to - newline-delimited
// JSON-RPC 2.0 over Net::LocalListener, so only this user's processes on this machine reach it. Its name is the
// single-instance lock: a server that cannot claim it does not start.
//     {"jsonrpc":"2.0","id":1,"method":"open","params":{"path":"C:/game/src/main.cf","line":42}}

include "bedrock.ch"

namespace Ied
{
	const int64 kControlLineBytes = 1048576;
	const int32 kAcceptSliceMs = 250;   // how soon the pipe closes once the server stops

	alias StopServer = func<void()>;

	// A response line, and whether the request was to stop the server.
	struct ControlAnswer
	{
		public Core::String line;
		public bool stop;
	}

	class Control
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server, StopServer stop) { this.server = server; this.stop = stop; }

		// Claims `name`; false with `error` when another server holds it.
		public bool Listen(Core::StringView name, Core::String& error);
		// Serves clients on a thread of its own, each connection on another.
		public void Start();

		// One request line to its response.
		public ControlAnswer Answer(Core::StringView line);

	private:
		private void AcceptLoop();
		private void Serve(Net::LocalStream* stream);
		private Core::String Call(Core::StringView method, const Serialization::DomNode& params);
		private Core::String Launch(Core::StringView fragment);
		private Core::String Status();
		private Core::String Open(const Serialization::DomNode& params);
		private void Stop();

		private Server* server;
		private StopServer stop;
		private Net::LocalListener listener;
		private Threading::Thread acceptor;
		private Threading::Atomic<bool> stopping;
	}

	// A JSON-RPC 2.0 response line: `result` JSON text, or an error with `code` and `message` when `code` is not 0.
	Core::String RpcResponse(Core::StringView id, Core::StringView result, int32 code, Core::StringView message);
}
