// The Package Manager's server side (docs/31 section 8). What the window shows of a workspace's Nuke packages lives
// here, on the topic packages:<wsId>, so it outlives the browser: the last status and browse results, what runs, and
// the last action's output. The bindings queue work and answer at once; one thread per workspace runs its queue in
// order, each job a `nuke package ...` command in the (trusted) workspace:
//   nuke.packages.refresh {wsId, fetch?}    `status --json [--fetch]`
//   nuke.packages.browse {wsId, source, offline?}   `browse <source> --json [--offline]`
//   nuke.packages.run {wsId, args: [...]}   `<args>`, the first an action (IsPackageAction); a status follows it
// The snapshot, and each "changed" event (the whole of it): {status, statusError, statusMs, browse: [{source, result,
// error, ms}], busy, queued, run: {id, args, running, exitCode, output, startedMs, endedMs} | null}; times are Unix ms.

include "api/Plugin.ch"
include "api/Process.ch"

namespace NukePlugin
{
	class PackageJob
	{
		public Core::String m_Kind;                 // "status", "browse" or "action"
		public Core::Array<Core::String> m_Words;   // what follows `nuke package`
		public Core::String m_Source;               // a browse's source
	}

	class BrowseResult
	{
		public Core::String m_Source;
		public Core::String m_Result;   // browse --json's document; "" when it failed
		public Core::String m_Error;
		public int64 m_Ms;
	}

	class PackageRun
	{
		public int64 m_Id;   // 0: no action yet
		public Core::Array<Core::String> m_Words;
		public bool m_Running;
		public int32 m_ExitCode;
		public Core::String m_Output;
		public int64 m_StartedMs;
		public int64 m_EndedMs;
	}

	// One workspace's packages as the Package Manager knows them; the server's lock guards all of it.
	class WorkspacePackages
	{
		public Core::String m_WsId;
		public Core::String m_Status;        // status --json's document; "" before one worked
		public Core::String m_StatusError;
		public int64 m_StatusMs;             // 0: the Package Manager never asked for it
		public Core::Array<BrowseResult> m_Browsed;
		public Core::Array<PackageJob> m_Queue;
		public Core::String m_Busy;          // what runs now ("status", "browse claymore", "update"); "" idle
		public bool m_Working;               // a thread runs the queue
		public int64 m_IdleMs;               // when the last job ended
		public PackageRun m_Run;
	}

	class PackageManager
	{
		[operator("create")] public void Init(IED::PluginContext* context) { m_Context = context; }

		// Each workspace's topic and the bindings.
		public void Start();
		// No job starts after this; one running ends by its time limit.
		public void Stop();

		private IED::Result Ask(Core::StringView wsId, const PackageJob& job);
		private void Queue(Core::StringView wsId, const PackageJob& job);
		private void Changed(const IED::FileChanges& changes);
		private void Work(Core::StringView wsId);
		private bool Next(Core::StringView wsId, PackageJob& job);
		private void Finish(Core::StringView wsId, const PackageJob& job, const IED::CommandResult& ran, Core::StringView problem);
		private IED::CommandResult Run(Core::StringView wsId, const PackageJob& job, Core::String& problem);
		private WorkspacePackages* Of(Core::StringView wsId);
		private Core::String SnapshotJson(WorkspacePackages* packages) const;

		private IED::PluginContext* m_Context;
		private Core::HashMap<Core::String, WorkspacePackages*> m_Workspaces;   // under the server's lock
		private bool m_Stopping;                                                 // under the server's lock
	}

	// The topic of workspace `wsId`'s packages.
	Core::String PackagesTopic(Core::StringView wsId);

	const int64 c_StatusSeconds = 180;
	const int64 c_ActionSeconds = 900;
}
