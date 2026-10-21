// One streamed chat completion put together from its chunks, in OpenAI's format as llama.cpp, vLLM, LM Studio and Ollama
// send it (docs/12 "Dialect normalization"): the content, the reasoning (reasoning_content, reasoning or thinking), the
// tool calls - deltas merged by id, then by index, so two calls at one index with different ids stay two - the finish
// reason, the usage and timings, and an error a server put in the stream. A chunk that is not JSON is counted and
// skipped, never fatal.

include "bedrock.ch"

namespace Ied
{
	class ToolCallDraft
	{
		public Core::String id;
		public Core::String name;
		public Core::String arguments;   // as streamed; may be cut or invalid
		public int64 index;
	}

	enum class DeltaKind { Content, Reasoning, ToolCall }

	// What one chunk added, for clients watching the stream.
	struct CompletionDelta
	{
		public DeltaKind kind;
		public Core::String text;
		public int64 call;          // the index in `calls` of a tool call's delta
	}

	class Completion
	{
		[operator("create")] public void Init() {}

		public Core::String content;
		public Core::String reasoning;
		public Core::Array<ToolCallDraft*> calls;
		public Core::String finishReason;   // empty when the stream gave none
		public bool done;                   // [DONE] arrived
		public bool errored;
		public Core::String errorMessage;
		public int64 promptTokens;
		public int64 completionTokens;
		public double promptPerSecond;      // llama.cpp's timings, 0 without them
		public int64 malformedChunks;
		public int64 chunks;

		// Takes one SSE data payload; `deltas` gets what it added.
		public void Take(Core::StringView data, Core::Array<CompletionDelta>& deltas);

		private void TakeChoice(const Serialization::DomNode& choice, Core::Array<CompletionDelta>& deltas);
		private void TakeToolCall(const Serialization::DomNode& delta, Core::Array<CompletionDelta>& deltas);
		private void TakeUsage(const Serialization::DomNode& root);
	}
}
