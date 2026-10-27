// A repository of a workspace (docs/10 "Git"): where it is, what its git:<repoId> topic last said (head, changes, refs
// as JSON text, so a refresh publishes only what changed), what is waiting to be read again, and the operation
// running in it. Also the JSON the git methods answer with.

include "Parse.ch"
include "api/Plugin.ch"

namespace Git
{
	const int64 c_GitListLimit = 5000;
	const int64 c_GitOpLines = 20;

	// A fetch, pull or push: one at a time per repository.
	class GitOp
	{
		public Core::String m_OpId;
		public Core::String m_Kind;
		public Core::String m_Title;
		public Core::Array<Core::String> m_Lines;   // the last kGitOpLines

		// {opId, kind, title, lines}
		public Core::String ToJson() const;
	}

	class GitRepo
	{
		public Core::String m_RepoId;
		public Core::String m_WsId;          // the workspace it was found in
		public FS::Path m_WsRoot;
		public FS::Path m_Root;
		public FS::Path m_GitDir;           // where HEAD and the index are
		public FS::Path m_CommonDir;        // where the refs are (a worktree's differs from gitDir)
		public Core::String m_Relative;     // workspace-relative root: "" the folder itself, ".." when above it
		public Core::String m_Name;
		public Core::String m_Kind;         // repo, submodule
		public Core::String m_HeadJson;
		public Core::String m_ChangesJson;
		public Core::String m_RefsJson;
		public GitOp* m_Op;
		public bool m_StatusDirty;
		public bool m_RefsDirty;
		public int64 m_ChangedNs;           // the last change seen
		public int64 m_ReadNs;              // when the status was last read
		public int64 m_ReadGeneration;      // reads started; what a read older than the last published one found is dropped
		public int64 m_StatusGeneration;    // the read the status published came from
		public int64 m_RefsGeneration;      // the read the refs published came from
		public FS::Watcher* m_OutsideWatch; // the common dir when it is not under the workspace folder

		public Core::String Topic() const { return Core::String("git:") + m_RepoId.View(); }
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
		public Core::String m_Path;
	}

	struct GitRefsRead
	{
		public GitRefs m_Refs;
		public Core::Array<GitRemote> m_Remotes;
		public Core::Array<GitStash> m_Stashes;
		public Core::Array<GitWorktree> m_Worktrees;
		public Core::Array<GitSubmodule> m_Submodules;
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
