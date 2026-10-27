// Terminal profiles (docs/05 "Terminal profiles"): the kinds of terminal the plugins loaded for a workspace contribute
// (terminalProfiles) - a program with its arguments, variables and folder - each with whether it can start (its program
// is there) and which is the default: the terminal.defaultProfile setting's, else the terminal.shell setting's program
// (no profile), else the first that can start of the terminal plugin's own (PowerShell 7, Windows PowerShell, the
// Command Prompt).

include "api/contributions.ch"
include "api/process.ch"
include "api/plugin.ch"

namespace Terminal
{
	class Profile
	{
		public Core::String id;
		public Core::String title;
		public Core::String icon;
		public Core::String plugin;                     // the plugin that contributes it
		public Core::Array<Core::String> argv;          // expanded; argv[0] the program file when it was found
		public Core::Array<Core::String> environment;   // "NAME=value", expanded
		public Core::String cwd;                        // expanded; empty: the workspace's folder
		public bool available;
		public Core::String problem;                    // why it cannot start

		// {id, title, icon, plugin, available, problem}
		public Core::String SummaryJson() const;
	}

	// `ws`'s profiles whose `when` holds there: the terminal plugin's own, then the others in the plugins' load order;
	// of one id the last loaded plugin's.
	Core::Array<Profile*> ProfilesOf(IED::PluginContext* context, const IED::Workspace& ws);
	// The profile of `id` among `profiles`; null when none has it.
	Profile* ProfileNamed(const Core::Array<Profile*>& profiles, Core::StringView id);
	// The default profile's id: the terminal.defaultProfile setting's when that profile can start, else empty when the
	// terminal.shell setting names a program (the shell runs it), else the first that can start of `fallbacks`; empty
	// when none can.
	Core::String DefaultProfile(IED::PluginContext* context, const IED::Workspace& ws, const Core::Array<Profile*>& profiles, const Core::Array<Core::String>& fallbacks);
	// {profiles: [summary], default, setting}: `setting` the terminal.defaultProfile setting as it is
	Core::String ProfilesJson(const Core::Array<Profile*>& profiles, Core::StringView defaultId, Core::StringView setting);
	// The terminal.defaultProfile setting.
	Core::String DefaultSetting(IED::PluginContext* context, Core::StringView wsId);
	// The terminal.shell setting's argv; empty when it is not set.
	Core::Array<Core::String> ShellSetting(IED::PluginContext* context, Core::StringView wsId);
}
