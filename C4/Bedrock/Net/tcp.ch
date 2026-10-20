// TcpStream and TcpListener: TCP connections (D-65). Each owns its socket and closes it on delete, so a value local is
// RAII; neither can be copied (move one with take()). A zero one is closed. A call that waits takes a timeout in
// milliseconds (kWaitForever, or 0 to only check); a call that fails keeps why in Error().
//     Net::TcpListener server;
//     server.Listen(Net::Address::Loopback(0));                          // port 0: any free port
//     Net::TcpStream client = Net::TcpStream::Connect(server.LocalAddress(), 2000);
//     Net::TcpStream accepted = server.Accept(2000);
//     client.SendText(Core::StringView("ping"));
//     char[4] reply;
//     accepted.ReceiveAll((void*)&reply[0], 4, 2000);

include "address.ch"
include "outcome.ch"
include "../Text/stringview.ch"

namespace Net
{
	// How long a call may wait: milliseconds, 0 to only check, or kWaitForever.
	const int32 kWaitForever = -1;

	enum class Shutdown { Receive = 0, Send = 1, Both = 2 }

	class TcpStream
	{
		[operator("create")] public void Init() {}   // closed
		[operator("delete")] public void Delete() { Close(); }

		// A connection to `to`; a closed stream with Error() when it is refused or unreachable, or TimedOut().
		public static TcpStream Connect(const Address& to, int32 timeoutMs);
		// Takes over a connected socket the runtime opened (TcpListener::Accept).
		public static TcpStream FromSocket(int64 socket);

		public bool IsOpen() const { return slot != 0; }
		// Bytes sent (TCP may take fewer than `count`), kTimedOut, or kError.
		public int64 Send(const void* bytes, int64 count, int32 timeoutMs = kWaitForever);
		// Every byte, or false (Error(), TimedOut()) when the connection fails or a send waits past `timeoutMs`.
		public bool SendAll(const void* bytes, int64 count, int32 timeoutMs = kWaitForever);
		public bool SendText(Core::StringView text, int32 timeoutMs = kWaitForever) { return SendAll((const void*)text.Data(), text.ByteLength(), timeoutMs); }
		// Bytes received, 0 once the peer has closed its side, kTimedOut, or kError.
		public int64 Receive(void* buffer, int64 capacity, int32 timeoutMs = kWaitForever);
		// Exactly `count` bytes, or false when the connection closes or fails first, or a receive waits past `timeoutMs`.
		public bool ReceiveAll(void* buffer, int64 count, int32 timeoutMs = kWaitForever);

		// Small writes go out at once instead of waiting to be combined (TCP_NODELAY).
		public bool SetNoDelay(bool on);
		// Ends one direction: after Shutdown::Send the peer's Receive returns 0, and this side can still receive.
		public void Shutdown(Net::Shutdown side);
		public Address LocalAddress() const;
		public Address PeerAddress() const;
		public void Close();   // idempotent

		// Why the last call failed; empty when it succeeded.
		public const char* Error() const { return last.error.CStr(); }
		// Whether the last call gave up waiting.
		public bool TimedOut() const { return last.timedOut; }
		// The runtime's socket (Net::Poll), -1 when closed.
		public int64 Socket() const { return slot - 1; }

		private int64 slot;   // the runtime's socket plus one: a zero stream has none
		private Outcome last;
	}

	class TcpListener
	{
		[operator("create")] public void Init() {}   // closed
		[operator("delete")] public void Delete() { Close(); }

		// Listens on `at` (port 0: a free port, which LocalAddress tells); false with Error() when the address is in use
		// or not this machine's. `backlog` 0 lets the system choose how many connections may wait.
		public bool Listen(const Address& at, int32 backlog = 0);
		// The next connection; a closed stream when none comes within `timeoutMs` (the listener's TimedOut()) or the
		// listener fails (its Error()).
		public TcpStream Accept(int32 timeoutMs = kWaitForever);

		public bool IsOpen() const { return slot != 0; }
		public Address LocalAddress() const;
		public void Close();   // idempotent
		public const char* Error() const { return last.error.CStr(); }
		public bool TimedOut() const { return last.timedOut; }
		public int64 Socket() const { return slot - 1; }

		private int64 slot;   // the runtime's socket plus one
		private Outcome last;
	}
}
