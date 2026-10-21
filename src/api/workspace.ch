// The workspaces a plugin sees (docs/26 section 2): an open workspace's identity and what it uses, and its files
// read and written through the server (so revisions and open documents stay right), each checked against the
// plugin's permissions (fs.read, fs.write) and the workspace's folder.

include "bedrock.ch"

namespace IED
{
	// A project of a solution workspace (docs/03).
	class Project
	{
		public Core::String name;
		public Core::String folder;      // its code root, relative to the workspace's root ("" for the root itself)
		public Core::String kind;        // "executable", "library", ...
		public Core::String language;    // its project file's `language`; empty when it names none
	}

	class Workspace
	{
		public Core::String id;
		public Core::String name;
		public Core::String root;        // its folder
		public Core::String kind;        // "folder", "file" (opened for one file) or "solution"
		public bool trusted;
		public Core::Array<Core::String> languages;   // the languages it uses (workspaceLanguage.<id>, docs/26 section 8)
		public Core::Array<Project> projects;         // a solution's projects; empty for a folder
	}

	alias WorkspaceHandler = func<void(const Workspace& ws)>;

	// The open workspace `wsId` as it is now; false when none is open.
	bool FindWorkspace(Core::StringView wsId, Workspace& ws);
	// The open workspaces the calling plugin is loaded for.
	Core::Array<Workspace> Workspaces();
	// A file of `ws` by its workspace-relative path; false with `problem` when it cannot be read.
	bool ReadFile(const Workspace& ws, Core::StringView path, Core::String& text, Core::String& problem);
	// Writes a file of `ws` (its folders made); false with `problem` when it cannot be written.
	bool WriteFile(const Workspace& ws, Core::StringView path, Core::StringView text, Core::String& problem);
}
