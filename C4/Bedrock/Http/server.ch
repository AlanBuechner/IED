// Server: an HTTP/1.1 server (D-66). Serve runs on the thread that calls it and accepts connections until Stop; each
// connection runs on a thread of its own and reads requests one after another (keep-alive), calling the handler for
// each, so the handler runs on several threads at once. A handler that throws answers 500.
//     Http::Server server;
//     server.Listen(Net::Address::Loopback(8080));
//     server.Serve([](const Http::Request& request, Http::Response& response) {
//         response.SetText(Core::StringView("text/plain"), Core::StringView("hello"));
//     });

include "message.ch"
include "parse.ch"
include "Net/tcp.ch"
include "Threading/atomic.ch"
include "Threading/thread.ch"

namespace Http
{
	const int64 kDefaultMaxHeadBytes = 64 * 1024;
	const int64 kDefaultMaxBodyBytes = 16 * 1024 * 1024;
	const int32 kDefaultIdleTimeoutMs = 30000;
	const int32 kDefaultRequestTimeoutMs = 30000;

	// What a server allows; a zero field takes its default.
	struct ServerLimits
	{
		public int64 maxHeadBytes;       // a larger head is answered 431
		public int64 maxBodyBytes;       // a larger body is answered 413
		public int32 idleTimeoutMs;      // how long a connection may wait for its next request
		public int32 requestTimeoutMs;   // how long a request may take to arrive once it has started
	}

	alias Handler = func<void(const Request&, Response&)>;

	class Server
	{
		[operator("create")] public void Init() {}
		[operator("delete")] public void Delete() { listener.Close(); }

		// Listens on `at` (port 0: a free port, which LocalAddress tells); false with Error().
		public bool Listen(const Net::Address& at);
		public Net::Address LocalAddress() const { return listener.LocalAddress(); }
		public const char* Error() const { return listener.Error(); }
		public void SetLimits(const ServerLimits& wanted) { limits = wanted; }
		// Serves https and wss: each connection starts with a TLS handshake (D-84) proving the server with `tls`, which
		// the server keeps; one whose handshake fails or takes longer than the request timeout is closed. Before Serve.
		public void UseTls(Net::TlsIdentity tls) { identity = take(tls); }
		public bool UsesTls() const { return identity.IsLoaded(); }

		// Accepts and answers until Stop(), then waits for every connection to end and returns. Does nothing unless
		// listening.
		public void Serve(Handler handler);
		// Ends Serve: no new connection is accepted, and each one ends once it has answered the request it is on. Safe
		// from any thread, also from a handler.
		public void Stop() { stopping.Store(true); }
		public bool Stopping() const { return stopping.Load(); }

		private Net::TcpListener listener;
		private Threading::Atomic<bool> stopping;
		private ServerLimits limits;
		private Net::TlsIdentity identity;
	}
}
