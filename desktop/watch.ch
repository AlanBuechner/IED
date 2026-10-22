// The server watch (docs/27 "Starting the server"): a thread that notices the user's ied-server is gone while windows
// are open and starts it again, at most kServerRestarts times a minute. While the server answers, it keeps the app told
// which workspaces it has open, so the windows get theirs back on a server started again.

include "launcher.ch"

namespace Desktop
{
	const int32 kServerRestarts = 3;
	const int64 kRestartWindowNs = 60000000000;
	const int64 kWatchIntervalNs = 2000000000;
	const int64 kWatchSliceNs = 100000000;

	// How many restarts are left: at most kServerRestarts in any kRestartWindowNs.
	class RestartBudget
	{
		// True, counting one, when a restart at `now` (ns) is within the budget.
		public bool Allow(int64 now);
		public void Reset() { starts.Clear(); }

		private Core::Array<int64> starts;
	}

	class Watch
	{
		[operator("delete")] public void Delete() { Stop(); }

		public void Start(App* app);
		public void Stop();
		// A fresh budget, after the user asked to retry.
		public void Reset();
		public bool Running() const { return thread.IsJoinable(); }

	private:
		private void Loop();
		private void Check();
		private void Look();

		private App* app;
		private Threading::Thread thread;
		private Threading::Atomic<bool> stopping;
		private Threading::Mutex mutex;   // the budget and whether its end was reported
		private RestartBudget budget;
		private bool exhausted;
	}
}
