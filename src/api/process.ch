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

	const int64 kCommandKeptBytes = 1024 * 1024;
}
