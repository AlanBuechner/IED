// Staging part of a file (docs/10 git.stagePatch): the patch `git apply --cached` takes for one hunk of the file's
// diff, or for some of its lines.

include "Parse.ch"

namespace Git
{
	// The patch that stages hunk `hunk` of `diff` (a work tree diff), or with `reverse` the one that unstages it
	// (applied with -R to a staged diff). `chosen` names the hunk's lines to take (indexes into its lines; empty: all):
	// the other added lines are left out and the other removed ones kept as context - the other way round when
	// reversing - and the counts made to match. Empty when nothing would change.
	Core::String HunkPatch(const GitDiff& diff, int64 hunk, const Core::Array<int64>& chosen, bool reverse);
}
