// The core tools beyond files (docs/13): search_text over the workspace (the open documents as edited), shell_run in
// the workspace with its output in the Output window's "AI" source, build_run through the toolbar's build task
// (docs/06) with the problems it found, and problems_list over the Error List.

include "Tool.ch"
include "fs/Search.ch"

namespace Ai
{
	const int64 c_SearchToolMatches = 100;
	const int64 c_SearchLineShownBytes = 160;   // a longer matching line is cut, "..." after it
	const int64 c_ShellTailLines = 200;
	const int64 c_ShellTailBytes = 16 * 1024;
	const int64 c_ShellDefaultSeconds = 120;
	const int64 c_ShellMostSeconds = 1800;
	const int64 c_ProblemsMost = 100;
	const int64 c_ShellKeptBytes = 256 * 1024;
	const char* c_AiOutputSource = "ai";

	void AddWorkTools(ToolRegistry& registry);
	// What search_text shows of `result`: each file's path once with its matching lines under it, numbered, a long
	// line cut at kSearchLineShownBytes; then whether it stopped at kSearchToolMatches, or that nothing matched.
	Core::String SearchListing(const IED::SearchResult& result);
	// The last `lines` lines of `text`, at most `bytes` of them.
	Core::String TailOf(Core::StringView text, int64 lines, int64 bytes);
	// What a command started for a tool call needs: its argv or shell text, the folder it runs in (workspace-relative)
	// and how long it may run; `tool` names it in what the model and the owner read.
	class ToolCommand
	{
		public Core::Array<Core::String> m_Argv;
		public Core::String m_Shell;
		public Core::String m_Cwd;
		public int64 m_Seconds;
		public Core::String m_Tool;
	}
	// Runs `command` in the workspace with its output in the AI output source: "exit <code>" and the output's tail, or
	// for a tool whose output is data (`whole`) all of its stdout, at most kShellKeptBytes.
	ToolOutcome RunToolCommand(const ToolInvocation& call, const ToolCommand& command, bool whole);
	// Starts `command` as RunToolCommand does without waiting for it (a background task reads it): the child, or null
	// with `problem`.
	Process::Child* StartToolCommand(const ToolInvocation& call, const ToolCommand& command, Core::String& problem);
	// shell_run's command from its arguments: timeout_s, else `defaultSeconds`, at most `mostSeconds`.
	ToolCommand ShellCommandOf(const ToolInvocation& call, int64 defaultSeconds, int64 mostSeconds);
	// build_run: the task to its end, its errors and warnings as path:line; *call.cancelled stops it.
	ToolOutcome BuildRun(const ToolInvocation& call);
	// "path:line:col: severity: message" for each error and warning in a diag snapshot's files (docs/10), the paths
	// starting with `under` (empty: all), at most `most`; `errors` and `warnings` count them all.
	Core::String ProblemLines(const Serialization::DomNode& files, Core::StringView under, bool errorsOnly, int64 most, int64& errors, int64& warnings);
}
