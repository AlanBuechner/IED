// Finding a workspace's repositories (docs/10 "Git"): the one its folder is in (which may be above it), then those in
// its folders down to a depth, the folders fs.search leaves out by default skipped.

include "cli.ch"

namespace Git
{
	struct GitFound
	{
		public FS::Path root;
		public FS::Path gitDir;
		public FS::Path commonDir;
		public bool submodule;   // its .git is a file into a parent's modules/
	}

	// The repositories of `folder`, outermost first; false with `error` when git cannot run at all.
	bool FindRepositories(const GitCli& cli, const FS::Path& folder, int32 depth, Core::Array<GitFound>& found, Core::String& error);

	// `path` under `root` as a workspace-relative path ("" for root itself, ".." when `path` holds `root`); false when
	// it is neither.
	bool GitRelative(const FS::Path& root, const FS::Path& path, Core::String& relative);
}
