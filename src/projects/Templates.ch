// ProjectTemplates (docs/03 "New projects"): the templates the loaded plugins contribute (`projectTemplates`: the global
// plugins' and those of each trusted open workspace's own plugins, each once) and a project made from one - a new
// workspace, trusted and opened, or a project added to an open solution: the template's steps run first (the build
// system's own init: `nuke init NAME --language L --ied`), then its files are copied in with ${name} filled, none
// replacing a file that is there. The project.templates and project.create methods; members run with the server lock
// held, which a create releases while its steps run.

include "Steps.ch"
include "plugins/Host.ch"
include "web/Rpc.ch"

namespace Ied
{
	// A template as a plugin contributes it.
	class ProjectTemplate
	{
		public Core::String m_Key;                  // "<plugin id>/<template id>"
		public Plugin* m_Plugin;
		public Workspace* m_From;                   // the open workspace whose own plugin gives it; null for a global one
		public Serialization::DomNode m_Entry;
	}

	class ProjectTemplates
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// The templates IED knows now, each once: the global plugins' first, then each trusted open workspace's own.
		public Core::Array<ProjectTemplate*> All();
		// A project from the template `key` named `name`: a new workspace in the folder `location` (a full path), opened
		// and trusted, or a project added to `into`'s solution. Throws as project.create answers.
		public ProjectChange* Create(Core::StringView key, Core::StringView name, Core::StringView location, Workspace* into);

	private:
		private Core::String ListMethod(Call& call, const Params& params);
		private Core::String CreateMethod(Call& call, const Params& params);

		private Server* m_Server;
	}

	// Whether `made` can add a project to `ws`: a trusted solution whose folder holds one of the template's `into` files
	// (nuke.toml: a Nuke workspace, whose `nuke init NAME` adds the project).
	bool TemplateCanAdd(const ProjectTemplate* made, Workspace* ws);
	// Where a new project goes unless the person says: beside the project opened last, else in their home folder.
	Core::String DefaultProjectLocation(Server* server);
	// Whether `name` can name a new project: letters, digits, '_', '.' and '-', starting with a letter, a digit or '_'.
	bool IsProjectName(Core::StringView name);
	// `text` with every ${name} replaced by `name`.
	Core::String WithName(Core::StringView text, Core::StringView name);
}
