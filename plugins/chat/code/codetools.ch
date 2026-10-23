// The AI tool calls plugin code implements (docs/26 section 8): a `toolCalls` entry with `function` - a free function
// of the plugin's code, `IED::ToolResult <Name>(const IED::ToolCall& call)` - or with `binding`, a server binding whose
// JSON result is the tool's output. Both run on the plugin's thread with "plugin(<plugin id>/<tool>)" as their subject
// for the permission rules (docs/11), never in an untrusted workspace unless the entry says availableUntrusted.

include "tool.ch"

namespace Ai
{
	// The tool `entry` of plugin `pluginId` describes: a function of its code, or a binding; null with `problem` when it
	// names neither.
	ToolSpec* CodeTool(const Serialization::DomNode& entry, Core::StringView pluginId, Core::String& problem);
	// `plan` with what the plugin's `plan` function said of a call to `tool`: its summary's first line and its diff
	// (kPlanDiffMostBytes of it, cut at a line), the counts it gave or else the diff's; or its error, which fails the call.
	void MergePlan(ToolPlan& plan, const IED::ToolPlan& planned, Core::StringView tool);

	const int64 kCodeToolDefaultSeconds = 120;
	const int64 kPlanDiffMostBytes = 65536;
}
