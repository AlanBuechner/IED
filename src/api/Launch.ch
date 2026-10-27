// A project's launch (docs/25 "Launch configurations"): its debug configuration with every ${...} expanded, for a plugin
// that starts the project's program its own way - the GPU debugger's Launch for Capture (docs/22).

include "Workspace.ch"

namespace IED
{
	class Launch
	{
		public Core::String m_Project;
		public Core::String m_Name;
		public Core::String m_Type;
		public Core::String m_PreLaunchTask;   // the task to run first ("build"), "" for none
		public Core::String m_Configuration;   // the configuration as JSON: program, args, cwd, env, ...
		public Core::String m_Problem;         // why there is none
	}

	// The launch `name` (empty: the default) of `project` (empty: the startup project) of `ws`; process.spawn.
	Launch ResolveLaunch(const Workspace& ws, Core::StringView project, Core::StringView name);
}
