// The server watch (docs/27 "Starting the server"): a thread that notices the user's ied-server is gone while windows
// are open and starts it again, at most kServerRestarts times a minute. While the server answers, it keeps the app told
// which workspaces it has open, so the windows get theirs back on a server started again; and when the server asks to be
// relaunched (src/host/Relaunch.ch), it claims that and has the app relaunch.

include "Launcher.ch"

namespace Desktop
{
	const int32 c_ServerRestarts = 3;
	const int64 c_RestartWindowNs = 60000000000;
	const int64 c_WatchIntervalNs = 2000000000;
	const int64 c_WatchSliceNs = 100000000;

	// How many restarts are left: at most kServerRestarts in any kRestartWindowNs.
	class RestartBudget
	{
		// True, counting one, when a restart at `now` (ns) is within the budget.
		public bool Allow(int64 now);
		public void Reset() { m_Starts.Clear(); }

		private Core::Array<int64> m_Starts;
	}

	class Watch
	{
		[operator("delete")] public void Delete() { Stop(); }

		public void Start(App* app);
		public void Stop();
		// A fresh budget, after the user asked to retry.
		public void Reset();
		public bool Running() const { return m_Thread.IsJoinable(); }

	private:
		private void Loop();
		private void Check();
		private void Look();

		private App* m_App;
		private Threading::Thread m_Thread;
		private Threading::Atomic<bool> m_Stopping;
		private Threading::Mutex m_Mutex;   // the budget and whether its end was reported
		private RestartBudget m_Budget;
		private bool m_Exhausted;
	}
}
