// The AI tool calls plugin code implements (docs/26 section 8): a `toolCalls` entry with `function` - a free function
// of the plugin's code, `IED::ToolResult <Name>(const IED::ToolCall& call)` - or with `binding`, a server binding whose
// JSON result is the tool's output. Both run on the plugin's thread with "plugin(<plugin id>/<tool>)" as their subject
// for the permission rules (docs/11), never in an untrusted workspace unless the entry says availableUntrusted.

include "tool.ch"

namespace Ied
{
	// The tool `entry` of plugin `pluginId` describes: a function of `instance`, its code, or a binding (`instance` may
	// then be null); null with `problem` when it names neither.
	ToolSpec* CodeTool(const Serialization::DomNode& entry, CodeInstance* instance, Core::StringView pluginId, Core::String& problem);

	const int64 kCodeToolDefaultSeconds = 120;
}
