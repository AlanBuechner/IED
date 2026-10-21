// LocalStream and LocalListener (B67): a channel between this user's processes on this machine, found by name - a
// named pipe on Windows. No other machine and no other user can use one: remote clients are refused, only the current
// user may open it, and Connect refuses a listener that runs as anyone else. A second Listen on a name someone listens
// on fails, so a name is also a single-instance lock. Like the TCP pair, each owns its pipe and closes it on delete,
// neither can be copied (move one with take()), and a call that fails keeps why in Error().
//     Net::LocalListener server;
//     if (!server.Listen(Net::UserLocalName("ied").CStr())) ...   // another instance runs
//     Net::LocalStream client = Net::LocalStream::Connect(Net::UserLocalName("ied").CStr(), 2000);
//     Net::LocalStream accepted = server.Accept(2000);

include "outcome.ch"
include "tcp.ch"

namespace Net
{
	class LocalStream
	{
		[operator("create")] public void Init() {}   // closed
		[operator("delete")] public void Delete() { Close(); }

		// A connection to the listener at `name`; a closed stream with Error() when no one (or another user) listens
		// there, or TimedOut() when it stayed busy.
		public static LocalStream Connect(const char* name, int32 timeoutMs);
		// Takes over a connection the runtime opened (LocalListener::Accept).
		public static LocalStream FromPipe(void* pipe);

		public bool IsOpen() const { return pipe != nullptr; }
		// Bytes sent, kTimedOut, or kError.
		public int64 Send(const void* bytes, int64 count, int32 timeoutMs = kWaitForever);
		// Every byte, or false (Error(), TimedOut()).
		public bool SendAll(const void* bytes, int64 count, int32 timeoutMs = kWaitForever);
		public bool SendText(Core::StringView text, int32 timeoutMs = kWaitForever) { return SendAll((const void*)text.Data(), text.ByteLength(), timeoutMs); }
		// Bytes received, 0 once the other side has closed (what it sent first still arrives), kTimedOut, or kError.
		public int64 Receive(void* buffer, int64 capacity, int32 timeoutMs = kWaitForever);
		public void Close();   // idempotent

		public const char* Error() const { return last.error.CStr(); }
		public bool TimedOut() const { return last.timedOut; }

		private void* pipe;   // the runtime's pipe, not GC memory
		private Outcome last;
	}

	class LocalListener
	{
		[operator("create")] public void Init() {}   // closed
		[operator("delete")] public void Delete() { Close(); }

		// Claims `name`; false with Error() when someone listens there already or it is not a name (empty, a
		// backslash, too long).
		public bool Listen(const char* name);
		// The next client; a closed stream when none comes within `timeoutMs` (TimedOut()) or listening failed.
		public LocalStream Accept(int32 timeoutMs = kWaitForever);

		public bool IsOpen() const { return pipe != nullptr; }
		public void Close();   // idempotent
		public const char* Error() const { return last.error.CStr(); }
		public bool TimedOut() const { return last.timedOut; }

		private void* pipe;
		private Outcome last;
	}

	// "<base>-<the current user's identity>" ("ied-S-1-5-21-..."): a name no other user's program shares.
	Core::String UserLocalName(const char* base);
}
