// git.workingDiff (docs/04 "Attachments and @-mentions"): what the workspace's working trees change against HEAD, as one
// text - what a chat reads for an `@diff` mention. Another plugin calls it through the binding.

include "service.ch"

namespace Git
{
	const int64 kWorkingDiffDefaultBytes = 65536;
	// IED's per-machine state (docs/07), at any depth: never one of the owner's files, gitignored or not.
	const char* kNotWorkingFiles = ":(exclude,glob)**/.ied/local/**";

	// {wsId, maxBytes?} -> {text, truncated, repos}: each repository's `git diff HEAD` and the files it does not track
	// (but kNotWorkingFiles), under a line naming the repository's folder, cut at a line start past `maxBytes` (64 KiB by
	// default); refused for a workspace git does not run in.
	IED::Result WorkingDiff(GitPlugin* git, const Serialization::DomNode& args);
	// `text` cut at the last line start at or before `most` bytes; `truncated` says whether anything was left out.
	Core::StringView CutAtLine(Core::StringView text, int64 most, bool& truncated);
}
