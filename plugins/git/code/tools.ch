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

	// The repository a call works in and where it sits in the workspace.
	struct ToolRepo
	{
		public GitRepo* repo;
		public GitPlace place;
	}

	// git.query: {wsId, tool, args} -> {text}; a refusal says why the tool cannot answer.
	void ProvideQuery(GitPlugin* git);

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
	// The system message's git section: which repositories the workspace has and how to work in them ("" for none).
	Core::String SectionText(const GitWorkspace* found);
}
