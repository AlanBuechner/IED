// Terminals (docs/10 "Terminals"), the ied.terminal plugin's server code: a shell, or any program, under a pseudo
// console on the server, so it outlives every browser. Each has a term:<termId> topic (its output, and the last
// kTerminalScrollbackBytes of it for a client that comes back) and the workspace's terms:<wsId> topic lists them. One
// client at a time types (the lease); the others watch. A reader and a writer thread per terminal.
//
// The terminals' state changes only inside PluginContext::Atomically, together with the events that tell of it, so the
// topics' pulled snapshots read it whole; the methods read it there too.

include "queue.ch"
include "api/plugin.ch"
include "api/text.ch"

namespace Terminal
{
	const int64 kTerminalScrollbackBytes = 262144;
	const int32 kTerminalDefaultColumns = 120;
	const int32 kTerminalDefaultRows = 30;

	class Term
	{
		public Core::String termId;
		public Core::String wsId;
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
		// {terminal, cols, rows, scrollback}
		public Core::String SnapshotJson() const;
	}

	class Terminals
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context);

		public void Opened(const IED::Workspace& ws);
		// A terminal running `argv` (empty: the workspace's shell) in `cwd`; the caller (`holder`) types. Throws
		// unavailable when nothing could be started.
		public Term* Create(Core::StringView wsId, const Core::Array<Core::String>& argv, Core::StringView cwd, Core::StringView title, Core::StringView kind, Core::StringView holder, int32 cols, int32 rows);
		public Core::String ListJson(Core::StringView wsId) const;
		// The terms:<wsId> event `kind` for `terminal`. Inside a change.
		public void Changed(IED::Events& events, Term* terminal, const char* kind);
		public void ReadLoop(Term* terminal, Process::Child* child);

		public IED::PluginContext* context;
		public Core::Array<Term*> terminals;
		public int64 made;

	private:
		private Core::Array<Core::Array<Core::String>> ShellsFor(Core::StringView wsId);
	}

	Core::String TerminalsTopic(Core::StringView wsId);
	// The terminal methods (term.*) on the plugin's context.
	void RegisterMethods(Terminals* terminals);
}
