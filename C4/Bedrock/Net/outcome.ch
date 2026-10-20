// Outcome: what a socket's last call came to (D-65) - why it failed, empty when it succeeded, and whether it gave up
// waiting. TcpStream, TcpListener and UdpSocket each keep one.

include "../Text/string.ch"

namespace Net
{
	// What a call returns besides a byte count or a socket.
	const int64 kError = -1;
	const int64 kTimedOut = -2;

	struct Outcome
	{
		public Core::String error;
		public bool timedOut;

		// Keeps what a runtime call wrote into `why` and passes its result on.
		public int64 Record(int64 result, const char* why) {
			timedOut = result == kTimedOut;
			if (why[0] == 0) error.Clear();
			else error = Core::String(why);
			return result;
		}

		public int64 Fail(const char* why) {
			timedOut = false;
			error = Core::String(why);
			return kError;
		}
	}
}
