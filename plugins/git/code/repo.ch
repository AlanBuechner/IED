// A repository of a workspace (docs/10 "Git"): where it is, what its git:<repoId> topic last said (head, changes, refs
// as JSON text, so a refresh publishes only what changed), what is waiting to be read again, and the operation
// running in it. Also the JSON the git methods answer with.

include "parse.ch"
include "api/plugin.ch"

namespace Git
{
	const int64 kGitListLimit = 5000;
	const int64 kGitOpLines = 20;

	// A fetch, pull or push: one at a time per repository.
	class GitOp
	{
		public Core::String opId;
		public Core::String kind;
		public Core::String title;
		public Core::Array<Core::String> lines;   // the last kGitOpLines

		// {opId, kind, title, lines}
		public Core::String ToJson() const;
	}

	class GitRepo
	{
		public Core::String repoId;
		public Core::String wsId;          // the workspace it was found in
		public FS::Path wsRoot;
		public FS::Path root;
		public FS::Path gitDir;           // where HEAD and the index are
		public FS::Path commonDir;        // where the refs are (a worktree's differs from gitDir)
		public Core::String relative;     // workspace-relative root: "" the folder itself, ".." when above it
		public Core::String name;
		public Core::String kind;         // repo, submodule
		public Core::String headJson;
		public Core::String changesJson;
		public Core::String refsJson;
		public GitOp* op;
		public bool statusDirty;
		public bool refsDirty;
		public int64 changedNs;           // the last change seen
		public int64 readNs;              // when the status was last read
		public int64 readGeneration;      // reads started; what a read older than the last published one found is dropped
		public int64 statusGeneration;    // the read the status published came from
		public int64 refsGeneration;      // the read the refs published came from
		public FS::Watcher* outsideWatch; // the common dir when it is not under the workspace folder

		public Core::String Topic() const { return Core::String("git:") + repoId.View(); }
		// {repoId, root, name, kind}
		public Core::String SummaryJson() const;
		// {repoId, root, head, changes, refs, op}
		public Core::String SnapshotJson() const;
	}

	// head: {branch, oid, detached, upstream, ahead, behind, state}
	Core::String GitHeadJson(const GitHead& head, Core::StringView state);
	// changes: {staged, unstaged, untracked, conflicted}, each at most kGitListLimit long
	Core::String GitChangesJson(const GitStatus& status);

	struct GitSubmodule
	{
		public Core::String path;
	}

	struct GitRefsRead
	{
		public GitRefs refs;
		public Core::Array<GitRemote> remotes;
		public Core::Array<GitStash> stashes;
		public Core::Array<GitWorktree> worktrees;
		public Core::Array<GitSubmodule> submodules;
	}

	// refs: {branches, remotes, tags, stashes, worktrees, submodules}; `root` marks the current worktree
	Core::String GitRefsJson(const GitRefsRead& read, const FS::Path& root);
	// {commits: [{oid, parents, author, email, authorMs, subject}], more}
	Core::String GitLogJson(const Core::Array<GitCommit>& commits, bool more);
	// [{path, oldPath?, status, added, removed, binary}]
	Core::String GitFilesJson(const Core::Array<GitFileChange>& files);
	// {path, oldPath?, binary, hunks, truncated}
	Core::String GitDiffJson(Core::StringView path, Core::StringView oldPath, const GitDiff& diff, bool truncated);
}
