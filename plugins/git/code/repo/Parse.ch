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
		public Core::String m_Path;
		public Core::String m_OldPath;   // a rename's or a copy's source; empty otherwise
		public Core::String m_Status;
	}

	struct GitHead
	{
		public Core::String m_Branch;     // empty when detached or before the first commit
		public Core::String m_Oid;        // empty before the first commit
		public bool m_Detached;
		public Core::String m_Upstream;
		public int64 m_Ahead;
		public int64 m_Behind;
		public bool m_Gone;           // the upstream is named but no longer exists
	}

	struct GitStatus
	{
		public GitHead m_Head;
		public Core::Array<GitChange> m_Staged;
		public Core::Array<GitChange> m_Unstaged;
		public Core::Array<GitChange> m_Untracked;
		public Core::Array<GitChange> m_Conflicted;
	}

	GitStatus ParseStatus(Core::StringView porcelain);

	// A branch, a remote branch ("origin/main") or a tag (its commit, an annotated tag peeled).
	struct GitRef
	{
		public Core::String m_Name;
		public Core::String m_Oid;
		public Core::String m_Upstream;
		public int64 m_Ahead;
		public int64 m_Behind;
		public bool m_Current;
		public bool m_Gone;
	}

	struct GitRefs
	{
		public Core::Array<GitRef> m_Branches;
		public Core::Array<GitRef> m_RemoteBranches;
		public Core::Array<GitRef> m_Tags;
	}

	// The for-each-ref format these read, one ref a line.
	const char* c_GitRefFormat = "%(refname)%1f%(objectname)%1f%(*objectname)%1f%(upstream:short)%1f%(upstream:track)%1f%(HEAD)";
	GitRefs ParseRefs(Core::StringView text);

	struct GitCommit
	{
		public Core::String m_Oid;
		public Core::Array<Core::String> m_Parents;
		public Core::String m_Author;
		public Core::String m_Email;
		public int64 m_AuthorMs;
		public Core::String m_Subject;
	}

	// The log format these read (with -z between commits).
	const char* c_GitLogFormat = "%H%x1f%P%x1f%an%x1f%ae%x1f%at%x1f%s";
	Core::Array<GitCommit> ParseLog(Core::StringView text);

	struct GitFileChange
	{
		public Core::String m_Path;
		public Core::String m_OldPath;
		public Core::String m_Status;   // A, M, D, R, C, T
		public int64 m_Added;
		public int64 m_Removed;
		public bool m_Binary;
	}

	Core::Array<GitFileChange> ParseNameStatus(Core::StringView text);
	// Puts `--numstat -z`'s counts on the files of ParseNameStatus.
	void AddNumstat(Core::Array<GitFileChange>& files, Core::StringView text);

	struct GitHunk
	{
		public int64 m_OldStart;
		public int64 m_OldLines;
		public int64 m_NewStart;
		public int64 m_NewLines;
		public Core::String m_Header;               // the whole "@@ ... @@ context" line
		public Core::Array<Core::String> m_Lines;   // each starts with '+', '-', ' ' or '\' ("\ No newline at end of file")
	}

	struct GitDiff
	{
		public Core::Array<Core::String> m_FileHeader;   // the lines before the first hunk (diff --git, index, ---, +++)
		public bool m_Binary;
		public Core::Array<GitHunk> m_Hunks;
	}

	// The diff of one file.
	GitDiff ParseDiff(Core::StringView text);
	// "@@ -a,b +c,d @@ ..." into the hunk's numbers; false when it is not one.
	bool ParseHunkHeader(Core::StringView line, GitHunk& hunk);

	struct GitStash
	{
		public int64 m_Index;
		public Core::String m_Oid;
		public Core::String m_Message;
	}

	const char* c_GitStashFormat = "%H%x1f%gs";
	Core::Array<GitStash> ParseStashes(Core::StringView text);

	struct GitWorktree
	{
		public Core::String m_Path;
		public Core::String m_Oid;
		public Core::String m_Branch;   // without refs/heads/; empty when detached
		public bool m_Locked;
		public bool m_Bare;
	}

	Core::Array<GitWorktree> ParseWorktrees(Core::StringView text);

	struct GitRemote
	{
		public Core::String m_Name;
		public Core::String m_Url;
	}

	// `config --get-regexp ^remote\..*\.url$` lines: "remote.<name>.url <url>".
	Core::Array<GitRemote> ParseRemotes(Core::StringView text);

	// The pieces of `text` between `separator`s; a trailing separator makes no empty last piece.
	Core::Array<Core::StringView> SplitOn(Core::StringView text, char separator);
}
