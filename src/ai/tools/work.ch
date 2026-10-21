// The core tools beyond files (docs/13): search_text over the workspace (the open documents as edited), shell_run in
// the workspace with its output in the Output window's "AI" source, build_run through the toolbar's build task
// (docs/06) with the problems it found, and problems_list over the Error List.

include "tool.ch"

namespace Ied
{
	const int64 kSearchToolMatches = 100;
	const int64 kShellTailLines = 200;
	const int64 kShellTailBytes = 16 * 1024;
	const int64 kShellDefaultSeconds = 120;
	const int64 kShellMostSeconds = 1800;
	const int64 kProblemsMost = 100;
	const char* kAiOutputSource = "ai";

	void AddWorkTools(ToolRegistry& registry);
	// The last `lines` lines of `text`, at most `bytes` of them.
	Core::String TailOf(Core::StringView text, int64 lines, int64 bytes);
	// "path:line:col: severity: message" for each error and warning in a diag snapshot's files (docs/10), the paths
	// starting with `under` (empty: all), at most `most`; `errors` and `warnings` count them all.
	Core::String ProblemLines(const Serialization::DomNode& files, Core::StringView under, bool errorsOnly, int64 most, int64& errors, int64& warnings);
}
