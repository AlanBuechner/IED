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
	// A full path inside the workspace as its workspace path: under its local state (kept outside its folder) as
	// ".ied/local/...", under its root relative to it, under a linked project's root (a Nuke package's clone) as
	// "@Name/..."; any other path as given.
	Core::String WorkspacePathOf(const IED::Workspace& ws, Core::StringView path);
	// A tool path that names a project the solution links outside its folder (docs/03 "Linked projects" - what a Nuke
	// package is): "@InputLayers/src" is that project's src folder. `onDisk` that folder or file, and `shown` its workspace
	// path - with a trailing "/" when it is a folder, so a tool joins what it finds under it and the result reads back as it
	// was given. False when the path starts with no link the workspace's projects have, that link holds nothing there, or the
	// workspace is untrusted (whose "@x" paths are plain names in its folder, so `Workspace::At` reaches nothing outside it).
	// fs_read and fs_edit take these paths through the server; the tools that walk a folder on disk - fs_list, fs_glob,
	// search_text - go through this instead.
	bool LinkTargetOf(const IED::Workspace& ws, Core::StringView path, Core::String& onDisk, Core::String& shown);
	// The folder a tool's `path` names on disk - in the workspace's folder, or under a linked project's root
	// ("@Render/src/Render") -; empty when it names no folder (code_symbols outlines a folder's files by it).
	FS::Path FolderOnDisk(const IED::Workspace& ws, Core::StringView path);
	// The folder a command runs in, from a tool's `cwd`: the workspace's root ("" or "."), a folder of it, or a linked
	// project's ("@JRPGToolkit/src"); where it would be when no such folder is there (the caller says so).
	Core::String WorkingFolderOf(const IED::Workspace& ws, Core::StringView cwd);
	// What a tool says when its `cwd` names no folder - Windows says only that "the directory name is invalid".
	Core::String NoWorkingFolder(Core::StringView tool, const IED::Workspace& ws, Core::StringView folder);
	// What a tool's `path` that names no linked project says about it: the links it could have meant. "" when the solution
	// links none.
	Core::String LinkHint(const IED::Workspace& ws, Core::StringView path);
	// For an "@Name/..." path whose link the solution has but which holds nothing there: NearestFoldersHint and
	// MovedFoldersHint inside that link, its folders named by the path that reaches them ("@InputLayers/src holds the
	// folders: InputLayers/ tests/"). "" when no link begins `path` (the hints of the workspace's own folder say that).
	Core::String LinkMissHint(const IED::Workspace& ws, Core::StringView path);
	// For an "@Name/..." file path that misses: the one file named like it under that link's root, by the link's name
	// ("@ClaymoreEngine/src/ClaymoreEngine/Content/BuiltinAssets.cf"); "" when none or several, or the path names no link.
	Core::String SameNamedLinkedFile(const IED::Workspace& ws, Core::StringView path);
	// The files named like the path's file, as the tools name them: under its link's root when it begins with a linked
	// project's name ("@JRPGToolkitEditor/src/Building/BuildingEditor.ch" finds "@JRPGToolkitEditor/src/JRPGToolkitEditor/BuildingEditor.ch"),
	// else in the workspace's own folder.
	Core::Array<Core::String> SameNamedFiles(const IED::Workspace& ws, Core::StringView path);
	// What every tool says for a path that names no folder or file: where it looked (the linked project's folders when the
	// path is one's, the workspace's own otherwise) and the links it could have meant.
	Core::String MissingPathHint(const IED::Workspace& ws, Core::StringView path, const FS::Path& root, Core::StringView shown);
	// A call's path argument `name` as a workspace path: WorkspacePathOf, then ToolPath (bad_request when it is
	// neither inside the workspace nor workspace-relative).
	Core::String WorkspaceArgument(const ToolInvocation& call, const char* name);
	// The glob of the workspace's files named like `path` ("src/a/Renderer.ch": "**/Renderer.ch"): where an fs_read
	// that missed most likely meant.
	Core::String SameNamedGlob(Core::StringView path);
	// For a `path` that is not there: "; <its nearest existing parent> holds the folders: a/ b/ ..." (`shown` before
	// the parent: a root's alias), then "; its files: ..." (those named like the path's last part first) when the path names
	// a file (its last part has an extension) or that parent has no folders.
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
