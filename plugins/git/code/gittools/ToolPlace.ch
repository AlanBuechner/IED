// Where a repository sits in its workspace, for the AI's git tools (docs/13 "git"): they take and show paths relative
// to the workspace's root, as every other tool does, while git takes and prints them relative to the repository's. A
// repository in a folder of the workspace ("sub") has its paths under "sub/"; one above the workspace's folder (the
// folder is "plugins/git" in it) is seen only inside the folder.

include "bedrock.ch"

namespace Git
{
	struct GitPlace
	{
		public Core::String m_Folder;   // the repository's folder in the workspace; "" for the folder's own (above it too)
		public Core::String m_Within;   // the workspace folder's path in a repository above it; "" otherwise

		// A workspace path ("" or "." the folder itself) as git names it in the repository ("." its root); false when
		// the repository does not hold it.
		public bool ToRepo(Core::StringView path, Core::String& repoPath) const;
		// A path git printed as the workspace names it; false when it is outside the workspace's folder.
		public bool FromRepo(Core::StringView repoPath, Core::String& path) const;
		// What `git diff` and `git show` are given so that the paths they print are the workspace's.
		public Core::Array<Core::String> DiffOptions() const;
		// The repository as the tools name it: its folder, "." for the workspace's own.
		public Core::StringView Name() const;
	}

	// The place of a repository discovery found at `relative` ("", "sub", ".."); `within` is the workspace folder's path
	// in a repository above it.
	GitPlace PlaceFrom(Core::StringView relative, Core::StringView within);
	// A path as the model wrote it, '/'-separated with no "./" ("" for the folder itself); throws bad_request for ".."
	// and absolute paths.
	Core::String ToolPath(Core::StringView path);
}
