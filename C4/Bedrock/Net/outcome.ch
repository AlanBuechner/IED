// Outcome: what a socket's last call came to (D-65) - why it failed, empty when it succeeded, and whether it gave up
// waiting. TcpListener keeps one; TcpStream, LocalStream and UdpSocket keep one per direction (Outcomes).

include "Text/string.ch"
include "Threading/atomic.ch"

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

	// A connection's outcomes, its sends' apart from its other calls' (BUG-497): one thread may send while another
	// receives - a WebSocket's pushes beside its session - and each records only into its own Outcome, so neither frees
	// the text the other is writing. Last() is the latest call's, for Error() and TimedOut() on a stream one thread uses.
	struct Outcomes
	{
		public Outcome sending;     // Send's and SendAll's
		public Outcome receiving;   // every other call's: receives, and the calls that open or secure the connection

		public Outcome& Sent() {
			lastSent.Store(true, MemoryOrder::Relaxed);
			return sending;
		}
		public Outcome& Received() {
			lastSent.Store(false, MemoryOrder::Relaxed);
			return receiving;
		}
		public const Outcome& Last() const {
			if (lastSent.Load(MemoryOrder::Relaxed)) return sending;
			return receiving;
		}

		private Threading::Atomic<bool> lastSent;
	}
}
