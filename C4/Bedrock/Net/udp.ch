// UdpSocket: datagrams (D-65). It owns its socket and closes it on delete; it cannot be copied (move it with take()).
// A zero one is closed. A datagram arrives whole or not at all, possibly out of order or more than once.
//     Net::UdpSocket socket;
//     socket.Open(Net::Address::Loopback(0));
//     socket.SendTo("hi", 2, peer);
//     Net::Address from;
//     int64 got = socket.ReceiveFrom((void*)&buffer[0], 1500, from, 100);

include "address.ch"
include "outcome.ch"

namespace Net
{
	// Bytes in the largest datagram UDP over IPv4 can carry.
	const int64 kMaxDatagramBytes = 65507;

	class UdpSocket
	{
		[operator("create")] public void Init() {}   // closed
		[operator("delete")] public void Delete() { Close(); }

		// Binds to `at` (port 0: a free port, which LocalAddress tells); false with Error().
		public bool Open(const Address& at);
		// The datagram's bytes sent, kTimedOut or kError. `to` must be of the socket's family.
		public int64 SendTo(const void* bytes, int64 count, const Address& to, int32 timeoutMs = -1);
		// One datagram (its sender in `from`): its bytes, kTimedOut, or kError - also when it is larger than `capacity`.
		public int64 ReceiveFrom(void* buffer, int64 capacity, Address& from, int32 timeoutMs = -1);

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
