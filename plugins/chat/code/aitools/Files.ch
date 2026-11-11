// The file tools of the core set (docs/13 T-FS-1..5): fs_read, fs_write, fs_edit, fs_list and fs_glob. They see a file
// as the editors do - an open document's text, unsaved changes and all - and write through the document store, so an
// open editor shows the AI's change at once and a write is saved like the owner's (docs/13 rule 6).

include "Tool.ch"

namespace Ai
{
	const int64 c_ReadMostLines = 400;
	const int64 c_ReadMostBytes = 24 * 1024;
	const int64 c_ListMostEntries = 400;
	const int64 c_GlobMostPaths = 200;

	void AddFileTools(ToolRegistry& registry);
	// A file's text as the editors have it. Throws not_found.
	Core::String TextAsEdited(const IED::Workspace& ws, Core::StringView path);
	// Writes `text` to `path`: into its open document, then saved, or to the disk. Throws unavailable.
	void WriteThrough(const IED::Workspace& ws, Core::StringView path, Core::StringView text);
	// The text of a file the model sees: lines numbered from `first` (1-based), at most `most` lines and
	// kReadMostBytes, with how to read on when it stops early.
	Core::String NumberedLines(Core::StringView text, int64 first, int64 most);
	// A path a tool call names, workspace-relative: its "." parts dropped ("." is the root, as models often write it),
	// then normalized as every workspace path is ("..", a drive letter: bad_request).
	Core::String ToolPath(Core::StringView path);
	// The glob of the workspace's files named like `path` ("src/a/Renderer.ch": "**/Renderer.ch"): where an fs_read
	// that missed most likely meant.
	Core::String SameNamedGlob(Core::StringView path);
	// For fs_list's `path` that is no folder: "; <its nearest existing parent> holds the folders: a/ b/ ..." (`shown` before
	// the parent: a root's alias).
	Core::String NearestFoldersHint(const FS::Path& root, Core::StringView path, Core::StringView shown);
	// For the same path: "; folders named like <its last part> elsewhere: a/ b/", those whose names begin as its last part
	// does, anywhere in the root (where it was moved or renamed to); "" when there are none.
	Core::String MovedFoldersHint(const FS::Path& root, Core::StringView path, Core::StringView shown);
	// The one path of `found` outside the folders the tools skip (build output, .git, ...), "" when none or several:
	// the file an fs_read that missed meant, read in its place.
	Core::String OnlySourceFile(const Core::Array<Core::String>& found);
	// Why fs_glob refuses a pattern that starts with a root's alias ("ied:/plugins/**/*.cf" matches nothing: only root
	// takes one), with the call that means it - the folders before its first wildcard as root; "" for any other pattern.
	Core::String GlobRootHint(Core::StringView pattern);
	// `text` with each edit's `old` replaced by its `new`, all or none; false with `error` naming the edit that failed. An
	// edit whose new is its old is left out when another changes something, and named in `unchanged`; alone it fails.
	bool ApplyTextEdits(Core::StringView text, const Serialization::DomNode& edits, Core::String& result, Core::String& error, Core::String& unchanged);
}
