// Terminals (docs/10 "Terminals"), the ied.terminal plugin's server code: a shell, or any program, under a pseudo
// console on the server, so it outlives every browser. Each has a term:<termId> topic (its output, and the last
// kTerminalScrollbackBytes of it for a client that comes back) and the workspace's terms:<wsId> topic lists them. One
// client at a time types (the lease); the others watch. A reader and a writer thread per terminal.
//
// The terminals' state changes only inside PluginContext::Atomically, together with the events that tell of it, so the
// topics' pulled snapshots read it whole; the methods read it there too.

include "Queue.ch"
include "Profiles.ch"
include "api/Plugin.ch"
include "api/Text.ch"

namespace Terminal
{
	const int64 c_TerminalScrollbackBytes = 262144;
	const int32 c_TerminalDefaultColumns = 120;
	const int32 c_TerminalDefaultRows = 30;

	class Term
	{
		public Core::String m_TermId;
		public Core::String m_WsId;
		public Core::String m_Title;
		public Core::String m_Kind;        // shell, or what made it (an agent, a task)
		public Core::String m_Profile;     // the terminal profile it runs; empty for a program given as it is
		public Core::String m_Holder;      // the client that types
		public bool m_Alive;
		public int64 m_ExitCode;
		public int32 m_Cols;
		public int32 m_Rows;
		public Core::String m_Scrollback;
		public Process::Child* m_Child;
		public MessageQueue* m_Input;      // keystrokes, in order, for the writer thread

		public Core::String Topic() const { return Core::String("term:") + m_TermId.View(); }
		// {termId, wsId, title, kind, profile, alive, exitCode, holder}
		public Core::String SummaryJson() const;
		// {terminal, cols, rows, scrollback}
		public Core::String SnapshotJson() const;
	}

	class Terminals
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context);

		public void Opened(const IED::Workspace& ws);
		// A terminal running `argv` (empty: the terminal.shell setting's program, else the first of PowerShell 7, Windows
		// PowerShell and cmd that starts) in `cwd` with `environment` ("NAME=value" over the server's); the caller
		// (`holder`) types. Throws unavailable when nothing could be started.
		public Term* Create(Core::StringView wsId, const Core::Array<Core::String>& argv, const Core::Array<Core::String>& environment, Core::StringView cwd, Core::StringView title, Core::StringView profile, Core::StringView kind, Core::StringView holder, int32 cols, int32 rows);
		public Core::String ListJson(Core::StringView wsId) const;
		// The terms:<wsId> event `kind` for `terminal`. Inside a change.
		public void Changed(IED::Events& events, Term* terminal, const char* kind);
		public void ReadLoop(Term* terminal, Process::Child* child);

		public IED::PluginContext* m_Context;
		public Core::Array<Term*> m_Terminals;
		public int64 m_Made;

	private:
		private Core::Array<Core::Array<Core::String>> ShellsFor(Core::StringView wsId);
	}

	Core::String TerminalsTopic(Core::StringView wsId);
	// The terminal methods (term.*) on the plugin's context.
	void RegisterMethods(Terminals* terminals);
}
