// Developer mode (docs/28): IED's own source checkout, when the server runs from one - a root chats reach as "ied:/"
// (the ai.roots binding), the system-message section that tells them to fix a bug of IED or the C4 toolchain there, and
// the tools that rebuild it, run its gates, install and relaunch it (ied.developer.install, IED::RelaunchServer) and
// list its known bugs.

include "source.ch"
include "builds.ch"

namespace Developer
{
	// The open entries of a known-bugs.md: each "### BUG-NNN" heading with the line after it.
	Core::String OpenBugs(Core::StringView knownBugs);
	// The end of a command's output that fits `most` bytes, starting at a line.
	Core::String OutputTail(Core::StringView output, int64 most);

	void Activate(IED::PluginContext& context);

	IED::ToolResult RebuildTool(const IED::ToolCall& call);
	IED::ToolResult GatesTool(const IED::ToolCall& call);
	// nuke install-ied, then IED relaunched onto it; the chat goes on after the restart (IED::ToolResult::restarts).
	IED::ToolResult RelaunchTool(const IED::ToolCall& call);
	IED::ToolResult KnownBugsTool(const IED::ToolCall& call);

	const int64 kTailBytes = 6000;
	const int64 kBuildSeconds = 1800;
	const int64 kQuickSeconds = 30;
	const int64 kSectionTokens = 600;   // the system message section, its source path included
	const int64 kTailLines = 60;   // of a failed install's log, for ied_relaunch
}
