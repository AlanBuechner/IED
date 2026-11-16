// MachineLock (docs/26 section 2): a lock every process of this user on this machine sees - the master and each
// window's workspace server, and a test server beside them. It is a claimed local pipe name (Net::LocalListener: a named
// pipe's first instance on Windows, an abstract socket on Linux), which the system gives back when its process ends,
// however it ends: no lock file is left behind to clear by hand. What holds it is said in a note beside (a file in the
// temp folder), so whoever waits can tell the owner what it waits for.
//
// Name locks by what they guard and the plugin that takes them: `ied.developer.nuke:<checkout>`,
// `ied.tools.install:<folder>`. A process may hold any number of locks; holding the same name twice in one process is
// waiting for itself.

include "bedrock.ch"

namespace IED
{
	class MachineLock
	{
		[operator("create")] public void Init() {}
		[operator("delete")] public void Delete() { Release(); }

		// Takes the lock `name`, saying it is held for `what`; while another process holds it, waits at most `waitMs`
		// (0: not at all), giving up early once `cancelled` answers true (none: never). False when it was not had.
		public bool Take(Core::StringView name, Core::StringView what, int64 waitMs, func<bool()> cancelled);
		public bool Take(Core::StringView name, Core::StringView what, int64 waitMs);
		// Gives it back; nothing when it is not held. The process ending gives it back too.
		public void Release();
		public bool Held() const { return m_Pipe.IsOpen(); }
		// How long the last Take waited, and what held the lock then (its holder's `what`; empty when it was free).
		public int64 WaitedMs() const { return m_WaitedMs; }
		public Core::String WaitedFor() const { return m_WaitedFor; }

		private Net::LocalListener m_Pipe;
		private Core::String m_Note;
		private int64 m_WaitedMs;
		private Core::String m_WaitedFor;
	}

	// The pipe name the lock `name` claims: "ied-lock-" and 24 hex digits of the name's SHA-256.
	Core::String MachineLockPipe(Core::StringView name);
	// "2 min 5 s", "40 s": how long a wait was, for a message.
	Core::String WaitText(int64 ms);

	const int64 c_MachineLockPollMs = 200;
}
