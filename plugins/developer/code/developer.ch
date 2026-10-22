// Developer mode (docs/28): IED's own source checkout, when the server runs from one - a root chats reach as "ied:/"
// (the ai.roots binding), the system-message section that tells them to fix a bug of IED or the C4 toolchain there, and
// the tools that rebuild it, run its gates and list its known bugs.

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
	IED::ToolResult KnownBugsTool(const IED::ToolCall& call);

	const int64 kTailBytes = 6000;
	const int64 kBuildSeconds = 1800;
}
