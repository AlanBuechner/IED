// WorkspaceServers (the master, docs/01 "Processes"): one ied-server --role workspace per open workspace - started with
// a secret of its own, watched, started again when it exits unasked (at most kServerExitsPerMinute times a minute, then
// failed until someone restarts it), restarted or stopped on request, and stopped with the master. Each runs in the
// master's job, so it ends with the master however that ends. A keeper thread per server does the waiting; everything
// else runs with the server lock held unless it says otherwise.

include "bedrock.ch"
include "web/Rpc.ch"

namespace Ied
{
	enum class ServerState { Starting, Ready, Restarting, Stopping, Failed }

	const char* ServerStateName(ServerState state);

	const int32 c_ServerExitsPerMinute = 3;
	const int64 c_ServerReadyMs = 60000;      // how long a start may take before it counts as an exit
	const int64 c_ServerStopMs = 10000;       // how long a server may take to stop before it is ended
	const int64 c_ServerAwaitMs = 15000;      // how long a client's request waits for a server that is (re)starting
	const int64 c_RoutesSettleMs = 10000;     // how long the master waits for its plugins' code to register its routes

	class WorkspaceServer
	{
		public Core::String m_WsId;
		public Core::String m_Target;          // what its --workspace names: a solution file, a folder or a file
		public Core::String m_Root;
		public Core::String m_Control;         // its own pipe
		public Core::String m_Secret;          // new at every start (docs/11 "Workspace servers")
		public Core::String m_StateDir;
		public Core::String m_LogPath;
		public ServerState m_State;
		public int32 m_Port;
		public int64 m_Pid;
		public int64 m_StartedMs;
		public int32 m_Restarts;               // how many times the master started it again
		public int64 m_ExitCode;               // how its last run ended; -1 before one did
		public Core::String m_Problem;         // why it failed, or how its last run ended
		public Core::String m_RestartedAfter;  // what its next run's log begins with
		public Core::Array<int64> m_ExitsMs;   // its unasked exits, for the budget
		public bool m_RestartAsked;
		public bool m_Closing;
		public bool m_Shutdown;
		public bool m_Keeping;                // its keeper thread runs
		public Process::Child* m_Child;
		public Threading::ConditionVariable m_Changed;   // its state changed (under the server lock)
	}

	class WorkspaceServers
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// A server for the master's record `ws` (its target set), started on a keeper thread of its own - once Begin has
		// run: the master starts none before it holds its pipe, which they call.
		public void Start(Workspace* ws);
		public void Begin();
		// What the workspace's server opens from its next start on (a folder that became a solution: the solution file).
		public void Retarget(Core::StringView wsId, Core::StringView target);
		public WorkspaceServer* Find(Core::StringView wsId) const;
		// {state, pid, startedMs, restarts, exitCode?, problem?, log}, or null for a workspace with no server.
		public Core::String StateJson(Core::StringView wsId) const;
		// Waits (the lock released) until `wsId`'s server is ready, at most `ms`: false when it is not open, failed, or
		// still not ready; its port and secret otherwise.
		public bool AwaitReady(Core::StringView wsId, int64 ms, int32& port, Core::String& secret);
		// The server stopped and started again (also a failed one's restart).
		public void Restart(Core::StringView wsId);
		// The server stopped for good; the workspace leaves the list once it has (workspace.closed).
		public void Close(Core::StringView wsId);
		// Without the lock: every server told to stop, waited for (kServerStopMs), and ended if it has not; for the
		// master's own stop.
		public void StopAll();
		// Without the lock: the master's pipe name, for its workspace servers' --master.
		public Core::String MasterControl() const { return m_MasterControl; }
		// The lock is held (released while they answer): `method` asked of every ready workspace server's pipe - an
		// app-tier file the master wrote, read again by each before the master answers its own caller.
		public void TellAll(Core::StringView method, Core::StringView paramsJson);

	private:
		private void Keep(WorkspaceServer* record);
		private void AwaitKeepers();
		private bool StartProcess(WorkspaceServer* record, Core::String& problem);
		private bool AwaitStarted(WorkspaceServer* record, Core::String& problem);
		private void Exited(WorkspaceServer* record, int64 code, Core::StringView how);
		private void Changed(WorkspaceServer* record);
		private void AskStop(WorkspaceServer* record);
		private Core::Array<Core::String> Arguments(const WorkspaceServer* record) const;
		private Core::String RestartMethod(Call& call, const Params& params);
		private Core::String CloseMethod(Call& call, const Params& params);

		private Server* m_Server;
		private Core::String m_MasterControl;
		private Core::Array<WorkspaceServer*> m_Records;
		private bool m_Running;
	}

	// The key of a folder's per-machine state and of its server's files: the first 16 hex digits of the SHA-256 of its
	// trust key (docs/01 "Storage").
	Core::String FolderKey(Core::StringView root);
	// Whether two secrets are the same, in time that does not depend on where they differ.
	bool SameSecret(Core::StringView a, Core::StringView b);
}
