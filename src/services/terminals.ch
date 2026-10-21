// Terminals (docs/10 "Terminals"): a shell, or any program, under a pseudo console on the server, so it outlives every
// browser. Each has a term:<termId> topic (its output, and the last kTerminalScrollbackBytes of it for a client that
// comes back) and the workspace's terms:<wsId> topic lists them. One client at a time types (the lease); the others
// watch. A reader thread per terminal holds the server lock only to publish. Members run with the server lock held.

include "workspaces.ch"
include "lsp/queue.ch"
include "web/rpc.ch"

namespace Ied
{
	const int64 kTerminalScrollbackBytes = 262144;
	const int32 kTerminalDefaultColumns = 120;
	const int32 kTerminalDefaultRows = 30;

	class Terminal
	{
		public Core::String termId;
		public Workspace* ws;
		public Core::String title;
		public Core::String kind;        // shell, or what made it (an agent, a task)
		public Core::String holder;      // the client that types
		public bool alive;
		public int64 exitCode;
		public int32 cols;
		public int32 rows;
		public Core::String scrollback;
		public Process::Child* child;
		public MessageQueue* input;      // keystrokes, in order, for the writer thread

		public Core::String Topic() const { return Core::String("term:") + termId.View(); }
		// {termId, wsId, title, kind, alive, exitCode, holder}
		public Core::String SummaryJson() const;
	}

	class Terminals
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		public void WorkspaceOpened(Workspace* ws);
		// A terminal running `argv` (empty: the workspace's shell) in `cwd`; the caller (`holder`) types. Throws
		// unavailable when nothing could be started.
		public Terminal* Create(Workspace* ws, const Core::Array<Core::String>& argv, const FS::Path& cwd, Core::StringView title, Core::StringView kind, Core::StringView holder, int32 cols, int32 rows);

	private:
		private Terminal* Find(Core::StringView termId);
		private Core::Array<Core::Array<Core::String>> ShellsFor(Workspace* ws);
		private Process::Child* StartIn(const Core::Array<Core::String>& argv, const FS::Path& cwd, int32 cols, int32 rows, Core::String& error);
		private void Changed(Terminal* terminal, const char* kind);
		private Core::String ListJson(Workspace* ws);
		private Core::String Snapshot(Terminal* terminal);
		private void ReadLoop(Terminal* terminal, Process::Child* child);
		private void WriteLoop(Process::Child* child, MessageQueue* input);

		private Core::String CreateMethod(Call& call, const Params& params);
		private Core::String ListMethod(Call& call, const Params& params);
		private Core::String InputMethod(Call& call, const Params& params);
		private Core::String TakeLeaseMethod(Call& call, const Params& params);
		private Core::String ResizeMethod(Call& call, const Params& params);
		private Core::String RenameMethod(Call& call, const Params& params);
		private Core::String CloseMethod(Call& call, const Params& params);

		private Server* server;
		private Core::Array<Terminal*> terminals;
		private int64 made;
	}

	Core::String TerminalsTopic(const Workspace* ws);
}
