// Commands a plugin runs (docs/26 section 2): in a workspace's folder, under the plugin's process.spawn permission,
// only in a trusted workspace, with a time limit - for a tool the plugin wraps (a formatter, a code generator).

include "workspace.ch"

namespace IED
{
	class CommandResult
	{
		public bool started;
		public int32 exitCode;
		public Core::String output;    // standard output and error together, at most kCommandKeptBytes
		public Core::String problem;   // why it did not start or did not finish
	}

	// Runs `argv` in `cwd` (relative to the workspace's root), at most `seconds`.
	CommandResult RunCommand(const Workspace& ws, const Core::Array<Core::String>& argv, Core::StringView cwd, int64 seconds);

	// The program file `program` names: itself when it is a path to a file (".exe" and PATHEXT's endings tried when it
	// has none), else the first one of that name in a folder of PATH; empty when there is none.
	Core::String FindProgram(Core::StringView program);
	// The program found for the language tool `id` in `ws` (docs/26 "Language tools", ${tool:<id>}); empty when no plugin
	// loaded there names such a tool.
	Core::String ToolProgram(const Workspace& ws, Core::StringView id);
	// The language tools of `ws` as the host sees them (docs/26 "Language tools"): the tools:<wsId> snapshot, {trusted,
	// detected, detecting, install, busy, dismissed, tools: [{id, title, kind, plugin, state, path, version, source, pin,
	// problem, canInstall, installs, ...}]}; "" when the workspace is not open.
	Core::String LanguageToolsJson(const Workspace& ws);
	// `environment` ("NAME=value") with what a process started for `ws` needs to find the language tools IED found or
	// installed outside PATH (docs/26 "Language tools"): their folders first on PATH.
	Core::Array<Core::String> ToolEnvironment(const Workspace& ws, const Core::Array<Core::String>& environment);

	const int64 kCommandKeptBytes = 1024 * 1024;
}
