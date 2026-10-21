// Git (docs/10 "Git"), the ied.git plugin's server code: the repositories of each trusted workspace, found by a thread
// of the plugin's own and listed on gitrepos:<wsId>; each repository's git:<repoId> topic kept current from the
// workspace's file changes (a status read after 150 ms of quiet, at most one a second while changes keep coming); the
// git.* methods; and fetch, pull and push as operations that stream their progress. Git never runs in an untrusted
// workspace (a repository's configuration and hooks are programs).
//
// Threads: methods run on their requests' threads, the refresh loop and each operation on threads of their own; the
// plugin's state is under its own lock, never held while git runs, and taken before the server's (the API's calls take
// that one) - never after it.

include "repo.ch"
include "cli.ch"
include "api/plugin.ch"

namespace Git
{
	const int64 kGitQuietNs = 150000000;
	const int64 kGitStatusEveryNs = 1000000000;
	const int64 kGitLogDefault = 200;
	const int64 kGitLogMost = 2000;
	const int64 kGitDiffMostBytes = 2097152;
	const int64 kGitFileMostBytes = 4194304;
	const char* kGitOutput = "git";

	// The git side of a workspace: its repositories, or why it has none.
	class GitWorkspace
	{
		public Core::String wsId;
		public FS::Path root;
		public bool trusted;
		public Core::Array<GitRepo*> repos;
		public bool discover;          // waiting for the discovery thread
		public Core::String error;     // "untrusted", or why git cannot run

		public Core::String Topic() const { return Core::String("gitrepos:") + wsId.View(); }
		// {repos: [summary], error, discovering}
		public Core::String SnapshotJson() const;
	}

	class GitPlugin
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context);

		public void Opened(const IED::Workspace& ws);
		public void TrustChanged(const IED::Workspace& ws);
		// A workspace's file changes, its .git included: which repositories to read again.
		public void FilesChanged(const IED::FileChanges& changes);
		// Ends the refresh thread (the plugin is stopping).
		public void Stop();

		// The lock is held for these.
		public GitWorkspace* Find(Core::StringView wsId) const;
		public GitRepo* RepoFor(const IED::Params& params) const;
		public GitCli CliFor(Core::StringView wsId) const;
		public Core::String SettingText(Core::StringView key, Core::StringView wsId) const;
		// Runs git in `repo` with the lock let go; throws unavailable with git's words when it fails (`code` instead).
		public GitRun RunOrThrow(GitRepo* repo, const Core::Array<Core::String>& arguments, int32 timeoutMs, IED::ErrorCode code);
		// A command that changes something: logged to the workspace's Git output, the repository read again.
		public GitRun RunChange(GitRepo* repo, const Core::Array<Core::String>& arguments, IED::ErrorCode code);
		// Reads `repo` again (its refs too when they changed or `refs`) and publishes what differs.
		public void Refresh(GitRepo* repo, bool refs);
		public void Log(GitRepo* repo, Core::StringView text);
		public void Publish(GitRepo* repo, const char* kind, Core::StringView data);
		// The repository's topic's snapshot, as it is now.
		public void Snapshot(GitRepo* repo);

		public IED::PluginContext* context;
		public Threading::Mutex lock;
		public Threading::ConditionVariable wake;
		public Core::Array<GitWorkspace*> workspaces;
		public int64 repoCount;
		public int64 opCount;
		public bool stopping;

	private:
		private void Trusted(GitWorkspace* git, bool trusted);
		private void Forget(GitWorkspace* git);
		private void Mark(GitRepo* repo, Core::StringView path, bool inGitDir);
		private void Watch(GitRepo* repo);
		private void Listed(GitWorkspace* git);
		private void RefreshLoop();
		private bool DiscoverNext();
		private void Discover(GitWorkspace* git);
		private bool RefreshDue(int64& waitNs);
		private void PublishRead(GitRepo* repo, const Core::String& head, const Core::String& changes, const Core::String& refs);

		private Threading::Thread thread;
	}

	// The diff a repository answers with for `path` (docs/10 git.diff's arguments as given).
	Core::Array<Core::String> GitDiffArgs(Core::StringView path, Core::StringView oldPath, Core::StringView oid, bool hasParent, bool staged, int64 context);
	// The state a repository is in from what its git dir holds: "", merging, rebasing, cherryPicking, reverting, bisecting.
	Core::String GitRepoState(const FS::Path& gitDir);
	// The git.* methods on the plugin's context: those that read (methods.cf), change (changes.cf) and run an operation
	// (ops.cf); a name the context refuses is a line in the log.
	void RegisterMethods(GitPlugin* git);
	void RegisterReadMethods(GitPlugin* git);
	void RegisterChangeMethods(GitPlugin* git);
	void RegisterOpMethods(GitPlugin* git);
	void RegisterMethod(GitPlugin* git, const char* name, IED::MethodHandler handler);
}
