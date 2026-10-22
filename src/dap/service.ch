// Debugger (docs/25): the workspace's debug sessions - dbg.start runs the configuration's pre-launch task and then the
// adapter; controls are revision-checked, so two devices pressing Step at once step once; inspection asks the adapter
// for the client. The topic dbg:<wsId> lists the sessions; dbg:<wsId>:<sessionId> carries one's stops, output and
// end. Paths go to clients as workspace paths, or file: URIs (named, so they open read-only) for files outside it;
// lines and columns from 0. Members run with the server lock held.

include "session.ch"
include "breakpoints.ch"

namespace Ied
{
	const int64 kDebugSessionsKept = 10;

	class Debugger
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		public void WorkspaceOpened(Workspace* ws);

		// From sessions.
		public void StateChanged(DebugSession* session);
		public void StopShown(DebugSession* session);
		// The adapter is initialized: the breakpoints and exception filters, then configurationDone.
		public void Configure(DebugSession* session);
		public void Event(DebugSession* session, Core::StringView event, const Serialization::DomNode& body);
		public void Log(DebugSession* session, Core::StringView text);
		// The session's program has a process (its `usage`): what it uses is sampled while the session lives.
		public void ProgramNamed(DebugSession* session);
		// A file's breakpoints changed: every configured session gets them.
		public void BreakpointsChanged(Workspace* ws, Core::StringView path);

	private:
		private DapProvider* ProviderFor(Workspace* ws, Core::StringView type);
		private DebugSession* Find(Core::StringView sessionId);
		private void Keep(DebugSession* session);
		private void SendBreakpoints(DebugSession* session, Core::StringView path, DapThen then);
		private void SendExceptionFilters(DebugSession* session);
		private Core::String WorkspaceSnapshot(Workspace* ws);
		private Core::String SessionSnapshot(DebugSession* session);
		private Core::String ClientFrames(DebugSession* session, const Serialization::DomNode& frames);
		private void WriteClientSource(DebugSession* session, JsonWriter& json, const Serialization::DomNode& source);
		private Core::String Control(const Params& params, Core::StringView command, bool whenStopped);
		private void Sample(DebugSession* session, uint32 processId);

		private Core::String StartMethod(Call& call, const Params& params);
		private Core::String StopMethod(Call& call, const Params& params);
		private Core::String RestartMethod(Call& call, const Params& params);
		private Core::String StackTraceMethod(Call& call, const Params& params);
		private Core::String PassMethod(Call& call, const Params& params, Core::StringView command);
		private Core::String RequestMethod(Call& call, const Params& params);
		private Core::String SessionsMethod(Call& call, const Params& params);

		private Server* server;
		private Core::Array<DebugSession*> sessions;
		private int64 started;
	}

	Core::String DebugTopic(const Workspace* ws);
	Core::String SessionTopic(const DebugSession* session);
}
