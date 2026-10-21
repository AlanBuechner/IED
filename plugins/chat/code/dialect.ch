// What local models emit outside OpenAI's fields (docs/12 "Dialect normalization"): reasoning inline as <think>...</think>
// in the content, and tool calls written as text - Hermes <tool_call>{json}</tool_call>, Qwen <function=name> with
// <parameter=key> blocks, Mistral [TOOL_CALLS][...] and [TOOL_CALLS]name[ARGS]{...}, a fenced JSON object with name and
// arguments, and harmony's to=functions.name ... <|message|>{...}<|call|>.

include "bedrock.ch"

namespace Ai
{
	// Splits streamed content into the reasoning inside <think>...</think> and the rest; a tag cut across two chunks is
	// held until the next one.
	class ThinkSplitter
	{
		[operator("create")] public void Init() {}

		// The new text of `chunk`: what is reasoning is appended to `thinking`, the rest to `visible`.
		public void Feed(Core::StringView chunk, Core::String& thinking, Core::String& visible);
		// What is held at the end of the stream.
		public void Flush(Core::String& thinking, Core::String& visible);
		// Inside a <think> that has not closed.
		public bool InThink() const { return inThink; }
		// A <think> was ever seen.
		public bool SawThink() const { return sawThink; }

		private Core::String held;
		private bool inThink;
		private bool sawThink;
	}

	// A tool call found in text: the tool's name and its arguments as JSON.
	struct TextToolCall
	{
		public Core::String name;
		public Core::String arguments;
	}

	// The tool calls `text` writes out in any of the dialects, in order; `rest` gets the text without them.
	Core::Array<TextToolCall> ParseTextToolCalls(Core::StringView text, Core::String& rest);
	// Whether `text` holds a tool-call opener of any dialect (used to tell an unparsed call from prose).
	bool LooksLikeTextToolCall(Core::StringView text);
	// Where the first tool-call opener in `text` starts; -1 when there is none.
	int64 TextToolCallStart(Core::StringView text);
	// Where the first tool-call opener in `text` starts; -1 when there is none.
	int64 TextToolCallStart(Core::StringView text);
}
