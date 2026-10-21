// Git (docs/10 "Git"): the repositories of each trusted workspace, found by a thread of the service's own and listed on
// gitrepos:<wsId>; each repository's git:<repoId> topic kept current from the workspace's file changes (a status read
// after 150 ms of quiet, at most one a second while changes keep coming); the git.* methods; and fetch, pull and push
// as operations that stream their progress. Git never runs in an untrusted workspace (a repository's configuration and
// hooks are programs). Members run with the server lock held; git runs with it let go.

include "repo.ch"
include "cli.ch"
include "web/rpc.ch"

namespace Ied
{
	const int64 kGitQuietNs = 150000000;
	const int64 kGitStatusEveryNs = 1000000000;
	const int64 kGitLogDefault = 200;
	const int64 kGitLogMost = 2000;
	const int64 kGitDiffMostBytes = 2097152;
	const int64 kGitFileMostBytes = 4194304;

	// The git side of a workspace: its repositories, or why it has none.
	class GitWorkspace
	{
		public Workspace* ws;
		public Core::Array<GitRepo*> repos;
		public bool discover;          // waiting for the discovery thread
		public Core::String error;     // "untrusted", or why git cannot run

		public Core::String Topic() const;
		// {repos: [summary], error, discovering}
		public Core::String SnapshotJson() const;
	}

	class Git
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		public void WorkspaceOpened(Workspace* ws);
		public void TrustChanged(Workspace* ws);
		// The workspace watcher's batch: which repositories to read again.
		public void FilesChanged(Workspace* ws, const ChangeBatch& batch);

	private:
		private GitWorkspace* Find(Workspace* ws) const;
		private GitRepo* RepoFor(const Params& params) const;
		private GitCli CliFor(Workspace* ws) const;
		private void Forget(GitWorkspace* git);
		private void Mark(GitRepo* repo, Core::StringView path, bool inGitDir);
		private void Watch(GitRepo* repo);

		// The thread: discoveries first, then the repositories that are due.
		private void RefreshLoop();
		private bool DiscoverNext();
		private void Discover(GitWorkspace* git);
		private bool RefreshDue(int64& waitNs);
		// Reads `repo` again (its refs too when they changed or `refs`) and publishes what differs.
		private void Refresh(GitRepo* repo, bool refs);
		private void Publish(GitRepo* repo, const Core::String& head, const Core::String& changes, const Core::String& refs);

		// Runs git in `repo` with the lock let go; throws unavailable with git's words when it fails (`code` instead).
		private GitRun RunOrThrow(GitRepo* repo, const Core::Array<Core::String>& arguments, int32 timeoutMs, ErrorCode code);
		// A command that changes something: logged to the workspace's Git output, the repository read again.
		private GitRun RunChange(GitRepo* repo, const Core::Array<Core::String>& arguments, ErrorCode code);
		private void Log(GitRepo* repo, Core::StringView text);

		private void RegisterMethods();
		private Core::String ReposMethod(Call& call, const Params& params);
		private Core::String LogMethod(Call& call, const Params& params);
		private Core::String ShowMethod(Call& call, const Params& params);
		private Core::String DiffMethod(Call& call, const Params& params);
		private Core::String FileAtMethod(Call& call, const Params& params);
		private Core::String StageMethod(Call& call, const Params& params);
		private Core::String UnstageMethod(Call& call, const Params& params);
		private Core::String StagePatchMethod(Call& call, const Params& params);
		private Core::String DiscardPatchMethod(Call& call, const Params& params);
		// git.stagePatch and git.discardPatch: the hunk (or its chosen lines) of the diff the caller saw, applied.
		private void ApplyHunk(const Params& params, bool discard);
		private Core::String DiscardMethod(Call& call, const Params& params);
		private Core::String CommitMethod(Call& call, const Params& params);
		private Core::String LastMessageMethod(Call& call, const Params& params);
		private Core::String CheckoutMethod(Call& call, const Params& params);
		private Core::String BranchMethod(Call& call, const Params& params);
		private Core::String BranchDeleteMethod(Call& call, const Params& params);
		private Core::String BranchRenameMethod(Call& call, const Params& params);
		private Core::String StashMethod(Call& call, const Params& params);
		private Core::String StashApplyMethod(Call& call, const Params& params);
		private Core::String StashDropMethod(Call& call, const Params& params);
		private Core::String OpMethod(Call& call, const Params& params, const char* kind);

		// An operation's thread: runs it, streams its lines, ends it.
		private void RunOp(GitRepo* repo, GitOp* op, Process::StartInfo info);
		private void OpLine(GitRepo* repo, GitOp* op, Core::StringView line);

		private Server* server;
		private Core::Array<GitWorkspace*> workspaces;
		private Threading::ConditionVariable wake;
		private Threading::Thread thread;
		private int64 repoCount;
		private int64 opCount;
	}

	// The diff a repository answers with for `path` (docs/10 git.diff's arguments as given).
	Core::Array<Core::String> GitDiffArgs(Core::StringView path, Core::StringView oldPath, Core::StringView oid, bool hasParent, bool staged, int64 context);
	// The state a repository is in from what its git dir holds: "", merging, rebasing, cherryPicking, reverting, bisecting.
	Core::String GitRepoState(const FS::Path& gitDir);
}
