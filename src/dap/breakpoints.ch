// Breakpoints (docs/25 "Breakpoints"): the workspace's, not a session's - set before any session, given to every one,
// kept per machine in .ied/local/breakpoints.json with the Watch window's expressions. bp.set replaces a file's, bp.list
// lists them, watch.set replaces the watches; the bp:<wsId> topic carries each file's breakpoints, the watches and, per
// session, where each breakpoint bound and whether the adapter verified it. Lines are 0-based, as every position a
// client sees (docs/10). Members run with the server lock held.

include "services/workspaces.ch"
include "web/rpc.ch"

namespace Ied
{
	class SourceBreakpoint
	{
		public int64 line;
		public bool enabled;
		public Core::String condition;
		public Core::String hitCondition;
		public Core::String logMessage;
	}

	// What a session's adapter made of one breakpoint.
	class BoundBreakpoint
	{
		public int64 line;        // the line it was set on
		public int64 id;          // the adapter's, 0 without one
		public bool verified;
		public int64 boundLine;   // where it bound (an adapter may move it to the next line with code)
		public Core::String message;
	}

	class WorkspaceBreakpoints
	{
		public Core::HashMap<Core::String, Core::Array<SourceBreakpoint*>> files;                                    // by path
		public Core::HashMap<Core::String, Core::HashMap<Core::String, Core::Array<BoundBreakpoint*>>> bound;        // session -> path
		public Core::Array<Core::String> watches;
	}

	class Breakpoints
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		public void WorkspaceOpened(Workspace* ws);
		// The paths with breakpoints, and a file's (none: an empty array).
		public Core::Array<Core::String> FilesOf(Workspace* ws);
		public Core::Array<SourceBreakpoint*> Of(Workspace* ws, Core::StringView path);
		// What a session's adapter made of `path`'s breakpoints (setBreakpoints answered).
		public void Bound(Workspace* ws, Core::StringView sessionId, Core::StringView path, const Core::Array<BoundBreakpoint*>& list);
		// The adapter changed one (a `breakpoint` event).
		public void Changed(Workspace* ws, Core::StringView sessionId, const Serialization::DomNode& breakpoint);
		public void SessionGone(Workspace* ws, Core::StringView sessionId);

	private:
		private WorkspaceBreakpoints* Get(Workspace* ws);
		private void Load(Workspace* ws);
		private void Save(Workspace* ws);
		private Core::String Snapshot(Workspace* ws);
		private Core::String FileJson(Core::StringView path, const Core::Array<SourceBreakpoint*>& list) const;
		private Core::String BoundJson(Core::StringView sessionId, Core::StringView path, const Core::Array<BoundBreakpoint*>& list) const;
		private Core::String SetMethod(Call& call, const Params& params);
		private Core::String ListMethod(Call& call, const Params& params);
		private Core::String WatchSetMethod(Call& call, const Params& params);
		private Core::String WatchesJson(Workspace* ws);

		private Server* server;
		private Core::HashMap<Core::String, WorkspaceBreakpoints*> workspaces;   // by wsId
	}

	Core::String BreakpointsTopic(const Workspace* ws);
}
