// Finding a workspace's repositories (docs/10 "Git"): the one its folder is in (which may be above it), then those in
// its folders down to a depth, the folders fs.search leaves out by default skipped.

include "Cli.ch"

namespace Git
{
	struct GitFound
	{
		public FS::Path m_Root;
		public FS::Path m_GitDir;
		public FS::Path m_CommonDir;
		public bool m_Submodule;   // its .git is a file into a parent's modules/
	}

	// The repositories of `folder`, outermost first; false with `error` when git cannot run at all.
	bool FindRepositories(const GitCli& cli, const FS::Path& folder, int32 depth, Core::Array<GitFound>& found, Core::String& error);

	// `path` under `root` as a workspace-relative path ("" for root itself, ".." when `path` holds `root`); false when
	// it is neither.
	bool GitRelative(const FS::Path& root, const FS::Path& path, Core::String& relative);

	// The workspace-relative folder whose .git the workspace-relative `path` is or is in ("libs/x" for
	// "libs/x/.git/HEAD", "" for ".git/index"); false when `path` is in no .git, or under a folder discovery skips.
	bool GitMarkerHolder(Core::StringView path, Core::String& holder);

	// Whether deleting the workspace-relative `path` takes the known repository whose workspace-relative folder is `repo`
	// ("" the workspace's own, ".." one above it) with it: its .git, its folder, or a folder above it - a folder deleted whole
	// is one change of its own path, never of the .git inside it.
	bool DeletionTakesRepo(Core::StringView path, Core::StringView repo);
}
