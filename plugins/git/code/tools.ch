// The AI's git tools (docs/13 "git", the manifest's toolCalls): each git_* function forwards to the git.query binding
// the plugin provides, which answers from the workspace's repositories as text (tooltext.ch). Paths are the
// workspace's (toolplace.ch); a repository is named by its folder, "." for the workspace's own. The tools that change
// a repository log to the Git output and read it again as the Git activity's do; none of them pushes or pulls.

include "service.ch"
include "tooltext.ch"

namespace Git
{
	const char* kQueryBinding = "git.query";
	const int64 kToolLogDefault = 30;
	const int64 kToolUntrackedMost = 50;
	const int64 kPlanPathsNamed = 3;   // the paths a card's summary names

	// The repository a call works in and where it sits in the workspace.
	struct ToolRepo
	{
		public GitRepo* repo;
		public GitPlace place;
	}

	// git.query: {wsId, tool, args} -> {text}; a refusal says why the tool cannot answer. With plan: true, what a write
	// tool's call will do, for its approval card: {summary, diff, plus, minus}, or {error} when the call cannot work.
	void ProvideQuery(GitPlugin* git);

	// What a write tool's call will do (toolplan.cf, docs/13 "What the approval card shows").
	class GitToolPlan
	{
		public Core::String summary;
		public Core::String diff;
		public int64 plus;
		public int64 minus;
	}

	// A card's diff, a file at a time: each file whole while the diff stays within `most`, then named with no lines.
	// The counts are every file's.
	class PlanDiff
	{
		// git diff's output: each of its files.
		public void AddDiff(Core::StringView diff);
		// One file's section: its header lines, then its hunks (or "Binary files ... differ").
		public void AddFile(Core::StringView section);
		// An untracked file as a new one, its lines from `git diff --no-index /dev/null <it>`; "" names it with none.
		public void AddNewFile(Core::StringView path, Core::StringView noIndex);

		public Core::StringBuilder text;
		public int64 most = kToolTextMostBytes;
		public int64 files;
		public int64 plus;
		public int64 minus;
		public int64 named;      // files past `most`, named with no lines
		public int64 unlisted;   // untracked files past kToolListMost, not named at all
	}

	// The `hunks` (1-based, in order) of one file's diff under its workspace path, each header ending "(hunk N)".
	Core::String HunksDiff(const GitDiff& diff, Core::StringView path, const Core::Array<int64>& hunks);
	// "3 files, +12 -4".
	Core::String CountWords(int64 files, int64 plus, int64 minus);
	// "a.c, b.c, c.c and 2 more".
	Core::String PathWords(const Core::Array<Core::String>& paths);

	// --- What the answers share (tools.cf); the lock is held ---

	// Throws the reason a workspace's git cannot answer: untrusted, no git, no repository yet.
	void RequireRepos(const GitWorkspace* found);
	GitPlace PlaceOf(const GitRepo* repo);
	// The repository a call names: `repo` (its folder), else the one holding `paths`, else the workspace's own or its
	// only one; throws not_found naming the repositories there are.
	ToolRepo ToolRepoFor(GitWorkspace* found, const Serialization::DomNode& args, const Core::Array<Core::String>& paths);
	// An array argument's strings.
	Core::Array<Core::String> ListArg(const Serialization::DomNode& args, const char* key);
	// `paths` as git names them in the repository; throws bad_request for one it does not hold.
	Core::Array<Core::String> RepoPaths(const ToolRepo& target, const Core::Array<Core::String>& paths);
	// A revision or a name argument ("" when absent); throws bad_request for one git would read as an option.
	Core::String NameArg(const Serialization::DomNode& args, const char* key);
	// Runs git with the lock let go and returns what it did, failed or not.
	GitRun TryRun(GitPlugin* git, GitRepo* repo, const Core::Array<Core::String>& arguments);
	// Whether the repository has no commit yet.
	bool IsUnborn(GitPlugin* git, GitRepo* repo);
	void AddArgs(Core::Array<Core::String>& arguments, const Core::Array<Core::String>& more);
	// One repository's git_status text.
	Core::String RepoStatus(GitPlugin* git, GitRepo* repo);

	// --- The answers: those that read (toolread.cf) and those that change (toolchange.cf) ---

	Core::String StatusAnswer(GitPlugin* git, GitWorkspace* found, const Serialization::DomNode& args);
	Core::String DiffAnswer(GitPlugin* git, GitWorkspace* found, const Serialization::DomNode& args);
	Core::String LogAnswer(GitPlugin* git, GitWorkspace* found, const Serialization::DomNode& args);
	Core::String ShowAnswer(GitPlugin* git, GitWorkspace* found, const Serialization::DomNode& args);
	Core::String BlameAnswer(GitPlugin* git, GitWorkspace* found, const Serialization::DomNode& args);
	Core::String BranchesAnswer(GitPlugin* git, GitWorkspace* found, const Serialization::DomNode& args);
	Core::String StageAnswer(GitPlugin* git, GitWorkspace* found, const Serialization::DomNode& args, bool stage);
	Core::String CommitAnswer(GitPlugin* git, GitWorkspace* found, const Serialization::DomNode& args);
	Core::String BranchAnswer(GitPlugin* git, GitWorkspace* found, const Serialization::DomNode& args);
	Core::String StashAnswer(GitPlugin* git, GitWorkspace* found, const Serialization::DomNode& args);
	// Their plans; each throws, as the tool would, the reason the call cannot work.
	GitToolPlan PlanStage(GitPlugin* git, GitWorkspace* found, const Serialization::DomNode& args, bool stage);
	GitToolPlan PlanCommit(GitPlugin* git, GitWorkspace* found, const Serialization::DomNode& args);
	GitToolPlan PlanBranch(GitPlugin* git, GitWorkspace* found, const Serialization::DomNode& args);
	GitToolPlan PlanStash(GitPlugin* git, GitWorkspace* found, const Serialization::DomNode& args);

	// --- What the changing tools and their plans share (toolchange.cf) ---

	// The hunks asked for, 1-based as git_diff numbers them, checked against `count` and sorted last first.
	Core::Array<int64> HunksArg(const Serialization::DomNode& args, int64 count, Core::StringView path);
	// Throws when a file's diff has no hunks to stage (or with `staged`, to unstage).
	void RequireHunks(const GitDiff& diff, Core::StringView path, bool staged);
	// Throws when HEAD is on its upstream: amending it would rewrite history others may have.
	void RequireUnpushed(GitPlugin* git, GitRepo* repo);
	bool HasRef(GitPlugin* git, GitRepo* repo, Core::StringView ref);
	// "stash@{2}".
	Core::String StashName(int64 index);
	// The system message's git section: which repositories the workspace has and how to work in them ("" for none).
	Core::String SectionText(const GitWorkspace* found);
}
