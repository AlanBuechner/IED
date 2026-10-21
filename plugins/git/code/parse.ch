// What the git CLI prints in its machine formats (docs/10 "Git"), read into plain values: `status --porcelain=v2 -z
// --branch`, `for-each-ref` and `log` with unit-separator (0x1f) fields, `diff --name-status -z` and `--numstat -z`, a
// unified diff, `stash list`, `worktree list --porcelain -z` and the remotes' urls from `config --get-regexp`.

include "bedrock.ch"

namespace Git
{
	// A path the index or the work tree changed: `status` is git's letter (A, M, D, R, C, T; "?" untracked; a
	// conflict's two letters).
	struct GitChange
	{
		public Core::String path;
		public Core::String oldPath;   // a rename's or a copy's source; empty otherwise
		public Core::String status;
	}

	struct GitHead
	{
		public Core::String branch;     // empty when detached or before the first commit
		public Core::String oid;        // empty before the first commit
		public bool detached;
		public Core::String upstream;
		public int64 ahead;
		public int64 behind;
	}

	struct GitStatus
	{
		public GitHead head;
		public Core::Array<GitChange> staged;
		public Core::Array<GitChange> unstaged;
		public Core::Array<GitChange> untracked;
		public Core::Array<GitChange> conflicted;
	}

	GitStatus ParseStatus(Core::StringView porcelain);

	// A branch, a remote branch ("origin/main") or a tag (its commit, an annotated tag peeled).
	struct GitRef
	{
		public Core::String name;
		public Core::String oid;
		public Core::String upstream;
		public int64 ahead;
		public int64 behind;
		public bool current;
		public bool gone;
	}

	struct GitRefs
	{
		public Core::Array<GitRef> branches;
		public Core::Array<GitRef> remoteBranches;
		public Core::Array<GitRef> tags;
	}

	// The for-each-ref format these read, one ref a line.
	const char* kGitRefFormat = "%(refname)%1f%(objectname)%1f%(*objectname)%1f%(upstream:short)%1f%(upstream:track)%1f%(HEAD)";
	GitRefs ParseRefs(Core::StringView text);

	struct GitCommit
	{
		public Core::String oid;
		public Core::Array<Core::String> parents;
		public Core::String author;
		public Core::String email;
		public int64 authorMs;
		public Core::String subject;
	}

	// The log format these read (with -z between commits).
	const char* kGitLogFormat = "%H%x1f%P%x1f%an%x1f%ae%x1f%at%x1f%s";
	Core::Array<GitCommit> ParseLog(Core::StringView text);

	struct GitFileChange
	{
		public Core::String path;
		public Core::String oldPath;
		public Core::String status;   // A, M, D, R, C, T
		public int64 added;
		public int64 removed;
		public bool binary;
	}

	Core::Array<GitFileChange> ParseNameStatus(Core::StringView text);
	// Puts `--numstat -z`'s counts on the files of ParseNameStatus.
	void AddNumstat(Core::Array<GitFileChange>& files, Core::StringView text);

	struct GitHunk
	{
		public int64 oldStart;
		public int64 oldLines;
		public int64 newStart;
		public int64 newLines;
		public Core::String header;               // the whole "@@ ... @@ context" line
		public Core::Array<Core::String> lines;   // each starts with '+', '-', ' ' or '\' ("\ No newline at end of file")
	}

	struct GitDiff
	{
		public Core::Array<Core::String> fileHeader;   // the lines before the first hunk (diff --git, index, ---, +++)
		public bool binary;
		public Core::Array<GitHunk> hunks;
	}

	// The diff of one file.
	GitDiff ParseDiff(Core::StringView text);
	// "@@ -a,b +c,d @@ ..." into the hunk's numbers; false when it is not one.
	bool ParseHunkHeader(Core::StringView line, GitHunk& hunk);

	struct GitStash
	{
		public int64 index;
		public Core::String oid;
		public Core::String message;
	}

	const char* kGitStashFormat = "%H%x1f%gs";
	Core::Array<GitStash> ParseStashes(Core::StringView text);

	struct GitWorktree
	{
		public Core::String path;
		public Core::String oid;
		public Core::String branch;   // without refs/heads/; empty when detached
		public bool locked;
		public bool bare;
	}

	Core::Array<GitWorktree> ParseWorktrees(Core::StringView text);

	struct GitRemote
	{
		public Core::String name;
		public Core::String url;
	}

	// `config --get-regexp ^remote\..*\.url$` lines: "remote.<name>.url <url>".
	Core::Array<GitRemote> ParseRemotes(Core::StringView text);

	// The pieces of `text` between `separator`s; a trailing separator makes no empty last piece.
	Core::Array<Core::StringView> SplitOn(Core::StringView text, char separator);
}
