// Solutions and projects (docs/03): a .ied.sln and the .ied.proj files it lists, read and validated against the host
// API's schemas, each project's code root resolved against its own file. Problems are kept per file with their JSON
// pointers; a broken project is listed with its problems rather than dropped.

include "../host/schemas.ch"
include "../fs/glob.ch"

namespace Ied
{
	class ProjectModel
	{
		public Core::String name;
		public FS::Path file;
		public FS::Path root;                  // the code root
		public Core::String kind;
		public Serialization::DomDoc* doc;
		public Serialization::DomNode data;    // the whole file, for the client
		public Core::Array<Core::String> problems;

		// {name, file, root, kind, problems, data}
		public Core::String ToJson() const;
	}

	class LocalPlugin
	{
		public Core::String id;      // the id the solution expects
		public FS::Path folder;
		public bool enabled;
	}

	class SolutionModel
	{
		public Core::String name;
		public FS::Path file;
		public FS::Path folder;
		public Serialization::DomDoc* doc;
		public Serialization::DomNode data;
		public Core::Array<ProjectModel*> projects;
		public Core::Array<LocalPlugin*> localPlugins;
		public Core::Array<Core::String> problems;
		public Core::Array<Core::String> watched;   // workspace-relative files a change in which means reading it again

		public ProjectModel* Project(Core::StringView name) const;
		// The project whose code root holds `path` (the deepest one); null for none.
		public ProjectModel* Owning(const FS::Path& path) const;
		// {name, file, problems, groups, items, generator, localPlugins: [{id, path, enabled}], projects: [...]}
		public Core::String ToJson() const;
	}

	// The solution at `file` (a canonical path) and its projects.
	SolutionModel* ReadSolution(const FS::Path& file, HostSchemas& schemas);
	// "<name>.ied.sln", in any case.
	bool IsSolutionFile(Core::StringView path);
}
