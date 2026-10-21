// Poll: waits on many sockets at once (D-65). Each entry names a socket (TcpStream::Socket(), TcpListener::Socket(),
// UdpSocket::Socket()) and what to wait for; Poll fills in what each is ready for. A listener is readable when a
// connection is waiting to be accepted.
//     Core::Array<Net::PollEntry> watched;
//     watched.Push(Net::PollEntry::For(listener.Socket(), Net::kReadable));
//     watched.Push(Net::PollEntry::For(client.Socket(), Net::kReadable));
//     if (Net::Poll(watched, 100) > 0 && (watched[0].ready & Net::kReadable) != 0) ...

include "Container/array.ch"

namespace Net
{
	// What an entry waits for and what it is ready for (bits).
	const int32 kReadable = 1;
	const int32 kWritable = 2;
	const int32 kClosed = 4;    // the peer hung up (found, never waited for)
	const int32 kFailed = 8;    // the socket failed or is not one (found, never waited for)

	// The runtime's layout (C4Runtime/NetRuntimeC.h, c4_net_poll_entry).
	struct PollEntry
	{
		public int64 socket;
		public int32 wanted;
		public int32 ready;

		public static PollEntry For(int64 socket, int32 wanted) {
			PollEntry entry;
			entry.socket = socket;
			entry.wanted = wanted;
			return entry;
		}
	}

	// Waits until an entry is ready for what it wants or `timeoutMs` passes (milliseconds, 0 to only check, -1 for
	// ever) and sets every entry's `ready`. How many are ready, 0 on a timeout, or -1. With no entries it only waits.
	int32 Poll(Core::Array<PollEntry>& entries, int32 timeoutMs);
}
