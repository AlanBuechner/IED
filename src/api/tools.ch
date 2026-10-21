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

	// An image the result shows the model: a file in the call's workspace. A model that sees images is sent it; one that
	// does not is told its path.
	class ToolImage
	{
		public Core::String mime;   // image/png, image/jpeg, image/webp or image/gif
		public Core::String path;   // relative to the workspace root (an absolute path inside it is accepted)
	}

	class ToolResult
	{
		public Core::String content;   // what the model is told
		public Core::String preview;   // what the tool card shows; the content when empty
		public bool isError;
		public Core::Array<ToolImage> images;
	}

	ToolResult ToolText(Core::StringView content);
	ToolResult ToolFailure(Core::StringView message);
}
