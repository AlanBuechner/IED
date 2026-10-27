// The workspaces a plugin sees (docs/26 section 2): an open workspace's identity and what it uses, and its files
// read and written through the server (so revisions and open documents stay right), each checked against the
// plugin's permissions (fs.read, fs.write) and the workspace's folder.

include "bedrock.ch"

namespace IED
{
	// A project of a solution workspace (docs/03).
	class Project
	{
		public Core::String m_Name;
		public Core::String m_Folder;      // its code root, relative to the workspace's root ("" for the root itself)
		public Core::String m_Kind;        // "executable", "library", ...
		public Core::String m_Language;    // its project file's `language`; empty when it names none
		public Core::String m_File;        // its project file, relative to the workspace's root when inside it
		public Core::Array<Core::String> m_References;   // the projects it depends on, by name
	}

	class Workspace
	{
		public Core::String m_Id;
		public Core::String m_Name;
		public Core::String m_Root;        // its folder
		public Core::String m_Kind;        // "folder", "file" (opened for one file) or "solution"
		public Core::String m_LocalState;  // its per-machine state (docs/07): a solution's .ied/local, a folder's in the app data
		public bool m_Trusted;
		public Core::Array<Core::String> m_Languages;   // the languages it uses (workspaceLanguage.<id>, docs/26 section 8)
		public Core::Array<Project> m_Projects;         // a solution's projects; empty for a folder
	}

	alias WorkspaceHandler = func<void(const Workspace& ws)>;

	// One path a workspace's watcher saw change: "created", "changed" or "deleted".
	class FileChange
	{
		public Core::String m_Path;      // workspace-relative
		public Core::String m_Change;
	}

	class FileChanges
	{
		public Core::String m_WsId;
		public Core::Array<FileChange> m_Changes;
		public bool m_Overflowed;        // too many to tell: anything may have changed
	}

	alias FilesHandler = func<void(const FileChanges& changes)>;

	// The open workspace `wsId` as it is now; false when none is open.
	bool FindWorkspace(Core::StringView wsId, Workspace& ws);
	// The open workspaces the calling plugin is loaded for.
	Core::Array<Workspace> Workspaces();
	// Opens `folder` (an absolute path) as a workspace, or finds the open one rooted there, as the owner opening it would:
	// its watcher, documents and plugins start, and clients list it (permission workspace.open; it does not go in the
	// recent projects). False with `problem` when it is no folder.
	bool OpenFolder(Core::StringView folder, Workspace& ws, Core::String& problem);
	// A file of `ws` by its workspace-relative path; false with `problem` when it cannot be read.
	bool ReadFile(const Workspace& ws, Core::StringView path, Core::String& text, Core::String& problem);
	// Writes a file of `ws` (its folders made); false with `problem` when it cannot be written.
	bool WriteFile(const Workspace& ws, Core::StringView path, Core::StringView text, Core::String& problem);
	// Moves a file or folder of workspace `wsId` to the recoverable trash (docs/11; fs.write).
	bool MoveToTrash(Core::StringView wsId, Core::StringView path, Core::String& problem);
	// `path` relative to `root` with '/' separators, ASCII case and separator kind not mattering; false when it is not
	// `root` or under it.
	bool RelativeTo(Core::StringView root, Core::StringView path, Core::String& relative);
	// The folders a workspace search leaves out ("**/node_modules", ...).
	Core::Array<Core::String> DefaultExcludes();
	// The file a workspace-relative path names: under .ied/local, in its local state (so a folder's stays out of the
	// folder), else under its root. Plugins join paths with it, never with `root`, for their state under .ied/local.
	Core::String WorkspaceFile(const Workspace& ws, Core::StringView path);
	// A workspace-relative path with '/' separators and no '.' or '..'; throws IED::RpcError bad_request.
	Core::String NormalizePath(Core::StringView path);
	// Whether `path` (workspace-relative) matches the glob `pattern` ("src/**/*.cf"; '*' within a name, '**' any folders).
	bool GlobMatch(Core::StringView pattern, Core::StringView path);
	// The files under `folder` that `pattern` matches, relative to it, in path order.
	Core::Array<Core::String> ExpandGlob(Core::StringView folder, Core::StringView pattern);
}
