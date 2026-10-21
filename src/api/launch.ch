// A project's launch (docs/25 "Launch configurations"): its debug configuration with every ${...} expanded, for a plugin
// that starts the project's program its own way - the GPU debugger's Launch for Capture (docs/22).

include "workspace.ch"

namespace IED
{
	class Launch
	{
		public Core::String project;
		public Core::String name;
		public Core::String type;
		public Core::String preLaunchTask;   // the task to run first ("build"), "" for none
		public Core::String configuration;   // the configuration as JSON: program, args, cwd, env, ...
		public Core::String problem;         // why there is none
	}

	// The launch `name` (empty: the default) of `project` (empty: the startup project) of `ws`; process.spawn.
	Launch ResolveLaunch(const Workspace& ws, Core::StringView project, Core::StringView name);
}
