// ProjectTemplates (docs/03 "New projects"): the templates the loaded plugins contribute (`projectTemplates`: the global
// plugins' and those of each trusted open workspace's own plugins, each once) and a project made from one - a new
// workspace, trusted and opened, or a project added to an open solution: the template's steps run first (the build
// system's own init: `nuke init NAME --language L --ied`), then its files are copied in with ${name} filled, none
// replacing a file that is there. The project.templates and project.create methods; members run with the server lock
// held, which a create releases while its steps run.

include "plugins/host.ch"
include "web/rpc.ch"

namespace Ied
{
	const int64 kTemplateStepMs = 120000;
	const int64 kTemplateLogBytes = 4000;

	// A template as a plugin contributes it.
	class ProjectTemplate
	{
		public Core::String key;                  // "<plugin id>/<template id>"
		public Plugin* plugin;
		public Workspace* from;                   // the open workspace whose own plugin gives it; null for a global one
		public Serialization::DomNode entry;
	}

	class ProjectTemplates
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// The templates IED knows now, each once: the global plugins' first, then each trusted open workspace's own.
		public Core::Array<ProjectTemplate*> All();

	private:
		private Core::String ListMethod(Call& call, const Params& params);
		private Core::String CreateMethod(Call& call, const Params& params);

		private Server* server;
	}

	// Whether `name` can name a new project: letters, digits, '_', '.' and '-', starting with a letter, a digit or '_'.
	bool IsProjectName(Core::StringView name);
	// `text` with every ${name} replaced by `name`.
	Core::String WithName(Core::StringView text, Core::StringView name);
}
