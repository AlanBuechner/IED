// The core tools beyond files (docs/13): search_text over the workspace (the open documents as edited), shell_run in
// the workspace with its output in the Output window's "AI" source, build_run through the toolbar's build task
// (docs/06) with the problems it found, and problems_list over the Error List.

include "Tool.ch"
include "fs/Search.ch"

namespace Ai
{
	const int64 c_SearchToolMatches = 100;
	const int64 c_SearchTooLargeShown = 5;   // files skipped for their size named in a search's answer
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
	// `unsearched` (from UnsearchedLinksNote) is said when nothing matched, so an empty answer never reads as the text
	// being nowhere when part of the solution was never looked at.
	Core::String SearchListing(const IED::SearchResult& result, Core::StringView unsearched = Core::StringView());
	// What search_text adds when it matched nothing and the solution links projects outside its folder, which a search of
	// the workspace's folder does not reach: "" when `scopes` already searched one of them or searched one file, or when the
	// solution links none - then the plain "no matches" is the whole answer.
	Core::String UnsearchedLinksNote(const IED::Workspace& ws, const Core::Array<SearchScope>& scopes);
	// Whether `folder` is, or is in, a folder search skips by default (build output, .git): search_text naming one
	// searches it whole.
	bool InExcludedFolder(Core::StringView folder);
	// Where search_text looks: m_File one file, read as edited and searched whatever ignore files and the skipped folders
	// say; else m_Alone a skipped folder searched whole on the disk ("" for the workspace as edited, which skips build
	// output and ignored files), or m_Disk a folder of its own on disk (a project the solution links outside its folder) with
	// the prefix its files are shown by, which is how it is read back; and the glob its files must match (relative to
	// m_Alone or m_Disk when either is set).
	struct SearchScope
	{
		public Core::String m_File;
		public Core::String m_Alone;
		public Core::String m_Disk;
		public Core::String m_Shown;
		public Core::String m_Glob;
	}
	// The scope of a search whose `path` is a folder - or one file, when `isFile` - and whose `glob` (under the folder) is
	// given or empty; a file's search is that file alone, its glob left out.
	SearchScope ScopeOfSearch(Core::StringView path, Core::StringView glob, bool isFile);
	// search_text's error for a failed search; a broken regex says how to match its special characters as text.
	Core::String SearchProblem(Core::StringView problem, bool regex);
	// search_text's error for a `path` (from `root`, shown after `shown`) that names no file or folder, with what MissingPathHint
	// says of it; "" when it names one, or nothing.
	Core::String MissingSearchPath(const IED::Workspace& ws, const FS::Path& root, Core::StringView path, Core::StringView shown);
	// The file a search_text `path` that names none most likely meant, which it searches instead (as fs_read reads it): the only
	// file of its name under `root` outside build output; "" for a folder's name (no extension), or when none or several have it.
	Core::String SameNamedSearchFile(const FS::Path& root, Core::StringView path);
	// Whether a search failed only because its pattern is no valid regex: search_text then searches it as text.
	bool IsBrokenRegex(Core::StringView problem);
	// What search_text says above the matches of a broken regex searched as text: why it was not one.
	Core::String SearchedAsTextNote(Core::StringView problem);
	// The program program_run starts for `program`: a file of the workspace at `root` - a path from the root, else from `folder`,
	// the one it runs in (a sub-project's own build output run in its folder), or a bare name of a file in `folder`, as cmd runs one
	// - else `program` as it is, a name looked for on PATH.
	Core::String ProgramFile(Core::StringView root, Core::StringView folder, Core::StringView program);
	// The last `lines` lines of `text`, at most `bytes` of them.
	Core::String TailOf(Core::StringView text, int64 lines, int64 bytes);
	// What a command started for a tool call needs: its argv or shell text, the folder it runs in (a tool's cwd: WorkingFolderOf)
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
