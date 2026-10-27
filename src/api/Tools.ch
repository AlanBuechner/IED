// AI tool calls in C4 (docs/26 section 8): a `toolCalls` entry with `function` names a free function of the plugin's
// code, `IED::ToolResult <Name>(const IED::ToolCall& call)`, run on the plugin's thread once the call's permission
// check and approval passed (docs/11, docs/13). Its `plan` names one, `IED::ToolPlan <Name>(const IED::ToolCall& call)`,
// that says what a call asking the owner will do, for the approval card.

include "Workspace.ch"

namespace IED
{
	class ToolCall
	{
		public Core::String m_Name;
		public Serialization::DomNode m_Args;   // checked against the entry's inputSchema
		public Workspace m_Workspace;
		public Core::String m_ChatId;
		public const bool* m_Cancelled;          // set when the owner stops the run

		public bool Cancelled() const { return m_Cancelled && *m_Cancelled; }
	}

	// An image the result shows the model: a file in the call's workspace. A model that sees images is sent it; one that
	// does not is told its path.
	class ToolImage
	{
		public Core::String m_Mime;   // image/png, image/jpeg, image/webp or image/gif
		public Core::String m_Path;   // relative to the workspace root (an absolute path inside it is accepted)
	}

	class ToolResult
	{
		public Core::String m_Content;   // what the model is told
		public Core::String m_Preview;   // what the tool card shows; the content when empty
		public bool m_IsError;
		// An error because what the tool ran failed (a command's exit code, failing tests), not the call itself: the
		// chat adds no failure notes to it (docs/12 "Tools execution").
		public bool m_ReportsFailure;
		public Core::Array<ToolImage> m_Images;
		// Where it ran, for the tool card ("MCP blender"); empty: the plugin.
		public Core::String m_Source;
		// The server restarts after this call (IED::RelaunchServer): the chat continues by itself once it is back, with a
		// message saying so (docs/12 "Restart behavior").
		public bool m_Restarts;
	}

	// What a call will do, made only by reading: the owner may deny it.
	class ToolPlan
	{
		public Core::String m_Summary;   // one line for the card: "Stage hunk 2 of src/ring.c"
		// The change as a unified diff: one file's hunks, or files each under "--- a/<path>" and "+++ b/<path>" lines
		// ("/dev/null" for a new or deleted file; paths the workspace's). A file with no hunks is named with no lines.
		public Core::String m_Diff;
		public int64 m_Plus;             // the lines the change adds and removes; both 0: counted from the diff
		public int64 m_Minus;
		public Core::String m_Error;     // the call cannot work as asked: it fails with this, without asking
	}

	ToolResult ToolText(Core::StringView content);
	ToolResult ToolFailure(Core::StringView message);
}
