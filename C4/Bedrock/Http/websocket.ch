// WebSocket: messages both ways over one connection (D-67, RFC 6455). A server's handler accepts an upgrade with
// Http::AcceptWebSocket(request, response, session): once the 101 response is out, the session runs with the
// connection's WebSocket on the connection's thread, and the connection closes when it returns (or the server stops).
// A client connects with WebSocket::Connect("ws://host:port/path", timeoutMs). Receive waits for a whole message -
// fragments joined, pings answered, the peer's close answered - and is for one thread at a time; the sends and Close
// are safe from any thread, so other threads can push while the session receives.
//     Http::AcceptWebSocket(request, response, [](Http::WebSocket& socket) {
//         Http::WebSocketMessage message;
//         while (socket.Receive(message, Net::kWaitForever)) socket.SendText(message.Text());
//     });

include "frames.ch"
include "message.ch"
include "../Net/tcp.ch"
include "../Threading/atomic.ch"
include "../Threading/mutex.ch"

namespace Http
{
	enum class MessageKind { Text, Binary }

	struct WebSocketMessage
	{
		public MessageKind kind;
		public Core::Array<uint8> data;

		public Core::StringView Text() const { return Core::StringView((const char*)data.Data(), data.Count()); }
	}

	// What one frame did for Receive: finished a message, needs another frame, or ended the connection.
	enum class Receipt { Message, Continue, Ended }

	const int64 kDefaultMaxMessageBytes = 16 * 1024 * 1024;
	// How long Close waits for the peer to answer its close frame.
	const int32 kCloseWaitMs = 1000;

	class WebSocket
	{
		[operator("create")] public void Init() {}
		[operator("delete")] public void Delete() { CloseStream(); }

		// A client's connection to "ws://host[:port]/path"; a closed socket with Error() when the connection or the
		// handshake fails.
		public static WebSocket Connect(Core::StringView url, int32 timeoutMs);
		// A server's WebSocket over a connection whose handshake was answered: it takes the stream and the bytes received
		// after the request. `stopping`, when not null, ends Receive once it is set (the server stopping).
		public static WebSocket Adopt(Net::TcpStream& stream, Core::Array<uint8>& received, const Threading::Atomic<bool>* stopping);

		public bool IsOpen() const { return stream.IsOpen(); }
		// The next whole message. False when the connection has closed (CloseCode() says how), failed (Error()), or no
		// message came within `timeoutMs` (TimedOut(); the socket stays open).
		public bool Receive(WebSocketMessage& message, int32 timeoutMs);
		public bool SendText(Core::StringView text) { return Send(Opcode::Text, (const void*)text.Data(), text.ByteLength()); }
		public bool SendBinary(const void* bytes, int64 count) { return Send(Opcode::Binary, bytes, count); }
		// At most 125 bytes, which the peer echoes in a pong (Receive takes it in).
		public bool Ping(Core::StringView payload);
		// Sends a close frame with `code` and `reason`, waits for the peer's answer unless another thread is receiving
		// (which then takes it), and closes.
		public void Close(int32 code, Core::StringView reason);

		// How the connection closed: the peer's code (kCloseNoCode when it gave none), the code this side closed with,
		// kCloseAbnormal when it ended without a close frame; 0 while open.
		public int32 CloseCode() const { return closeCode; }
		public const char* CloseReason() const { return closeReason.CStr(); }
		public const char* Error() const { return error.CStr(); }
		public bool TimedOut() const { return timedOut; }
		// When bytes last came from the peer (clock_now_ns), a message or not: a pong to Ping counts, so a server can
		// tell a quiet client from a gone one. 0 before the first.
		public int64 LastReceivedNs() const { return lastReceivedNs; }
		public void SetMaxMessageBytes(int64 bytes) { maxMessageBytes = bytes; }

		private bool Send(Opcode opcode, const void* bytes, int64 count);
		private bool SendClose(int32 code, Core::StringView reason);
		private void CloseStream();
		private bool ReceiveMore(int64 deadlineNs);
		private Receipt Take(Frame& frame, WebSocketMessage& message, bool& assembling);
		private void Fail(int32 code, Core::StringView why);
		private void Drain(int64 deadlineNs);

		private Net::TcpStream stream;
		private Core::Array<uint8> pending;
		private Threading::Mutex sending;     // held for every send and for closing the stream
		private Threading::Mutex receiving;   // held by Receive and by Close while it waits
		private const Threading::Atomic<bool>* stopping;
		private bool client;
		private bool closeSent;               // guarded by `sending`
		private int32 closeCode;
		private Core::String closeReason;
		private Core::String error;
		private bool timedOut;
		private int64 maxMessageBytes;        // 0: kDefaultMaxMessageBytes
		private int64 lastReceivedNs;
	}

	// Whether `request` asks to become a WebSocket: a GET over HTTP/1.1 with Upgrade: websocket, Connection: upgrade,
	// Sec-WebSocket-Version: 13 and a Sec-WebSocket-Key of 16 bytes in base64.
	bool IsWebSocketRequest(const Request& request);
	// The Sec-WebSocket-Accept answer to a Sec-WebSocket-Key.
	Core::String WebSocketAccept(Core::StringView key);
	// In a request handler: answers the handshake (101) and has `session` run once it is out. False, with a 400 (426
	// for another version) in `response`, when the request is not a WebSocket handshake.
	bool AcceptWebSocket(const Request& request, Response& response, func<void(WebSocket&)> session);
}
