// Mentions read when a turn starts (docs/04 "Attachments and @-mentions"): the text the model gets for each thing the
// owner's message names - mostly what the tool that reads it answers, so the model sees a mention as it would see that
// tool's result - and what it is told of the attachments it is not shown as images.

include "tool.ch"

namespace Ai
{
	const int64 kMentionLines = 200;          // an output source's or a terminal's last lines
	const int64 kMentionDiffBytes = 65536;
	const int64 kMentionChatTurns = 3;

	// The tool that reads a mention and its arguments as JSON; an empty tool for a kind no tool reads.
	class MentionCall
	{
		public Core::String tool;
		public Core::String args;
	}

	MentionCall MentionTool(const Mention& mention);
	// <mention label="@..." kind="...">text</mention>
	Core::String MentionBlock(const Mention& mention, Core::StringView text);
	// The last `most` lines of `text`, without a final empty one.
	Core::String TailLines(Core::StringView text, int64 most);
	// What the model is told of an attachment it is not shown: its path, size and type, to read with fs_read.
	Core::String AttachedNote(const Attachment& attachment);
	// Whether the attachment goes to the model as an image (one a request can send, at most kToolImageMaxBytes).
	bool AttachedImage(const Attachment& attachment);
	// The mention's text, read now; why it could not be read when it cannot. `call` is the turn's, without a tool's
	// arguments. The server lock must not be held.
	Core::String ReadMention(const ToolRegistry& tools, const ToolInvocation& call, const Mention& mention);
}
