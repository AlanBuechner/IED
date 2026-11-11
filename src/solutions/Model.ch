// Solutions and projects (docs/03): a .ied.sln and the .ied.proj files it lists, read and validated against the host
// API's schemas, each project's code root resolved against its own file. Problems are kept per file with their JSON
// pointers; a broken project is listed with its problems rather than dropped.

include "host/Schemas.ch"
include "fs/Glob.ch"

namespace Ied
{
	class ProjectModel
	{
		public Core::String m_Name;
		public FS::Path m_File;
		public FS::Path m_Root;                  // the code root
		public Core::String m_Path;              // the workspace path of its root: "" the solution's folder, "src/Game", or for a root outside it its link ("@Engine")
		public Core::String m_Link;              // a root outside the solution's folder: the workspace path that reaches it (docs/03 "Linked projects"); empty inside it
		public Core::String m_Kind;
		public Serialization::DomDoc* m_Doc;
		public Serialization::DomNode m_Data;    // the whole file, for the client
		public Core::Array<Core::String> m_Problems;

		// {name, file, root, path, link?, kind, problems, data}
		public Core::String ToJson() const;
	}

	class LocalPlugin
	{
		public Core::String m_Id;      // the id the solution expects
		public FS::Path m_Folder;
		public bool m_Enabled;
	}

	class SolutionModel
	{
		public Core::String m_Name;
		public FS::Path m_File;
		public FS::Path m_Folder;
		public Serialization::DomDoc* m_Doc;
		public Serialization::DomNode m_Data;
		public Core::Array<ProjectModel*> m_Projects;
		public Core::Array<LocalPlugin*> m_LocalPlugins;
		public Core::Array<Core::String> m_Problems;
		public Core::Array<Core::String> m_Watched;   // workspace-relative files a change in which means reading it again

		public ProjectModel* Project(Core::StringView name) const;
		// The projects its groups name that no project file gives, in the order they are named (a generated solution
		// whose generator has not run since its groups changed).
		public Core::Array<Core::String> UngivenProjects() const;
		// The project whose code root holds `path` (the deepest one); null for none.
		public ProjectModel* Owning(const FS::Path& path) const;
		// The file a workspace path starting with a link names ("@Engine/src/a.cf"); empty when no link begins it.
		public FS::Path LinkedFile(Core::StringView path) const;
		// The workspace path of `file` under a linked project's root (the deepest that holds it); false when none does.
		public bool LinkedPath(const FS::Path& file, Core::String& path) const;
		// {name, file, problems, groups, items, generator, localPlugins: [{id, path, enabled}], projects: [...]}
		public Core::String ToJson() const;
	}

	// The solution at `file` (a canonical path) and its projects.
	SolutionModel* ReadSolution(const FS::Path& file, HostSchemas& schemas);
	// Each project's root as the workspace sees it (m_Path): a path in the solution's folder, or outside it a link of its
	// own name (m_Link, "@<name>").
	void PlaceRoots(SolutionModel* solution);
	// What begins the workspace path of a linked project's root: "@<project>".
	const char* c_LinkPrefix = "@";

	// "<name>.iedsln", in any case; "<name>.ied.sln" too, the name before (docs/03 "Files").
	bool IsSolutionFile(Core::StringView path);
	// "<name>.iedproj", or "<name>.ied.proj", the name before.
	bool IsProjectFile(Core::StringView path);
	// The one solution file directly in `folder`, which opening the folder opens; empty when there is none, or more
	// than one to choose from.
	FS::Path SolutionIn(const FS::Path& folder);
}
