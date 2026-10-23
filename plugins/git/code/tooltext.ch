// The AI's git tools' answers as text a model reads well (docs/13 "git"), made from what git prints in its machine
// formats, every path the workspace's (toolplace.ch).

include "parse.ch"
include "toolplace.ch"

namespace Git
{
	const int64 kToolListMost = 200;          // the entries of each list an answer shows
	const int64 kToolTextMostBytes = 24576;   // a diff's, a file's or a blame's text in one answer
	const int64 kToolFileMostLines = 400;

	// The log format git_log reads, with -z between commits and --date=format:%Y-%m-%d %H:%M.
	const char* kToolLogFormat = "%h%x1f%ad%x1f%an%x1f%D%x1f%s";
	// The for-each-ref format git_branches reads: parse.ch's with each ref's subject after it.
	const char* kToolRefFormat = "%(refname)%1f%(objectname)%1f%(*objectname)%1f%(upstream:short)%1f%(upstream:track)%1f%(HEAD)%1f%(contents:subject)";

	// One repository's status: its branch and how it stands against its upstream, the state it is in (a merge, a
	// rebase, ...), then its changes - staged, not staged, untracked, conflicted - at most kToolListMost of each.
	Core::String StatusText(const GitStatus& status, Core::StringView state, const GitPlace& place, Core::StringView name);
	// A unified diff: a line of counts per file, then the diff, cut at a line past `most` bytes with what was left out
	// said. With `numbered`, each hunk's header ends with its number in its file, "(hunk 2)", as git_stage counts them.
	Core::String DiffText(Core::StringView diff, bool numbered, int64 most);
	// `log -z` records of kToolLogFormat, a line a commit, at most `most` of them; when there are more, how to page on
	// past the `skip` already skipped.
	Core::String LogText(Core::StringView records, int64 most, int64 skip);
	// `blame --porcelain` as the commits its lines come from, then each line with its number and commit.
	Core::String BlameText(Core::StringView porcelain, Core::StringView path);
	// for-each-ref lines of kToolRefFormat and the stashes: the branches with their upstreams and last commits, the
	// remote branches, the tags, the stashes.
	Core::String BranchesText(Core::StringView refs, const Core::Array<GitStash>& stashes);
	// A file's text as it was at a revision, lines numbered from `first`, at most `most` lines and kToolTextMostBytes,
	// with how to read on.
	Core::String FileText(Core::StringView text, int64 first, int64 most);
	// What git's status letters say: "modified", "renamed", "both modified", ...
	Core::StringView ChangeWord(Core::StringView status);
}
