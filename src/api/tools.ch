// AI tool calls in C4 (docs/26 section 8): a `toolCalls` entry with `function` names a free function of the plugin's
// code, `IED::ToolResult <Name>(const IED::ToolCall& call)`, run on the plugin's thread once the call's permission
// check and approval passed (docs/11, docs/13).

include "workspace.ch"

namespace IED
{
	class ToolCall
	{
		public Core::String name;
		public Serialization::DomNode args;   // checked against the entry's inputSchema
		public Workspace workspace;
		public Core::String chatId;
		public const bool* cancelled;          // set when the owner stops the run

		public bool Cancelled() const { return cancelled && *cancelled; }
	}

	class ToolResult
	{
		public Core::String content;   // what the model is told
		public Core::String preview;   // what the tool card shows; the content when empty
		public bool isError;
	}

	ToolResult ToolText(Core::StringView content);
	ToolResult ToolFailure(Core::StringView message);
}
