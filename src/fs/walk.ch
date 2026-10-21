// FileWalk: the files of a folder tree in name order, as workspace-relative paths ('/' separators), passing over the
// folders the exclusions name and what .gitignore files ignore - for search and for finding files by name. Honest:
// it reads only the disk under `root`.

include "ignore.ch"

namespace Ied
{
	// Called with each file's path; false stops the walk.
	alias FileVisitor = func<bool(Core::StringView)>;

	class FileWalk
	{
		public FS::Path root;
		public Core::Array<Core::String> exclude;   // globs; a folder or file they match is passed over
		public bool useIgnoreFiles;
		public const Threading::Atomic<bool>* cancel;   // set: the walk stops soon; null: never

		// Visits every file; false when the visitor or `cancel` stopped it.
		public bool Run(const FileVisitor& visit) { return Folder(Core::StringView(""), Core::Array<IgnoreFile*>(), visit); }

	private:
		private bool Folder(Core::StringView folder, Core::Array<IgnoreFile*> ignores, const FileVisitor& visit);
	}

	// Whether `path` matches one of `globs`.
	bool MatchesAny(const Core::Array<Core::String>& globs, Core::StringView path);
}
