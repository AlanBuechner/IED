// Projects for plugins (docs/26 section 2, docs/03 "New projects" and "Managing projects"): the templates New Project
// offers, a project made from one, an existing folder made a solution or a project of one, and a solution's project
// removed or moved - each as the protocol's project.* methods do it, the open workspace's model read again at once.
// Permission workspace.projects. Each false with `problem` when the change was refused or failed.

include "bedrock.ch"

namespace IED
{
	class ProjectTemplate
	{
		public Core::String key;            // "<plugin>/<id>", what CreateProject takes
		public Core::String title;
		public Core::String description;
		public Core::String language;
		public Core::Array<Core::String> tags;
		public bool canAdd;                 // it can add a project to the solution asked about
	}

	// What a project made of a folder is; an empty field is left to IED or the build system.
	class ProjectChoices
	{
		public Core::String name;
		public Core::String kind;           // executable, library, ... (the build system's or the project file's)
		public Core::String language;
	}

	class ProjectChange
	{
		public Core::String wsId;           // the workspace that shows the result
		public Core::String workspaceName;
		public Core::String workspaceKind;
		public Core::String path;           // the project's folder, or the solution file made
		public Core::String project;
		public Core::String trashId;        // a deleted project's folder in the trash
		public Core::Array<Core::String> notes;
		public Core::String log;            // the end of what the steps printed
	}

	// The templates, whether each can add a project to `wsId`'s solution ("": none asked about), and where a new
	// workspace goes unless the person says.
	bool ProjectTemplates(Core::StringView wsId, Core::Array<ProjectTemplate>& templates, Core::String& location, Core::String& problem);
	// A project from the template `key`: a new workspace `<location>/<name>`, opened and trusted (`intoWsId` ""), or a
	// project added to the solution `intoWsId`.
	bool CreateProject(Core::StringView key, Core::StringView name, Core::StringView location, Core::StringView intoWsId, ProjectChange& change, Core::String& problem);
	// The folder at the full path `folder` made an IED solution; an open folder workspace there becomes it in place.
	bool ImportFolder(Core::StringView folder, const ProjectChoices& choices, ProjectChange& change, Core::String& problem);
	// The folder `folder` of `wsId` (workspace-relative) made a project of its solution.
	bool AddProject(Core::StringView wsId, Core::StringView folder, const ProjectChoices& choices, ProjectChange& change, Core::String& problem);
	// The project `name` taken out of `wsId`'s solution, its folder kept or, with `deleteFiles`, moved to the trash.
	bool RemoveProject(Core::StringView wsId, Core::StringView name, bool deleteFiles, ProjectChange& change, Core::String& problem);
	// The project `name`'s folder moved to `to` (workspace-relative).
	bool MoveProject(Core::StringView wsId, Core::StringView name, Core::StringView to, ProjectChange& change, Core::String& problem);
}
