// One model request of a run (docs/12 "The loop": a step): the streamed POST read under the watchdogs - the prefill
// deadline until the first token (keepalive comments extend it), the idle one between chunks after it, a hard cap -
// and what arrived put together: reasoning from its fields and <think> blocks, text with any tool calls written as text
// taken out of it, native tool calls merged by id then index. Text is handed out as it streams, a short tail held back
// so a tool-call opener cut across chunks never shows as prose.

include "wire/completion.ch"

namespace Ied
{
	// How the request ended on the wire (docs/12 taxonomy, the transport and stall rows).
	enum class WireEnd { Completed, ConnectFailed, HttpError, Disconnected, StalledPrefill, StalledStream, StepCap, Cancelled }

	enum class StepEventKind { Thinking, Text, CallStarted, CallArguments }

	// What streams out of a step as it happens: thinking and text deltas, a native call starting (`text` its name, `call`
	// its index in the step's calls) and its argument deltas. Called on the step's thread.
	alias StepEvents = func<void(StepEventKind kind, Core::StringView text, int64 call)>;

	class StepRequest
	{
		public Core::String url;
		public Core::Array<Core::String> headers;   // "Name: value"
		public Core::String body;                   // the chat completion request, JSON
		public int32 connectMs;
		public int64 prefillMs;                     // until the first token
		public int64 prefillExtendMs;               // what a keepalive comment adds
		public int64 idleMs;
		public int64 capMs;
	}

	class StepResult
	{
		[operator("create")] public void Init() { completion = new Completion(); ttftMs = -1; }

		public WireEnd end;
		public int32 httpStatus;
		public Core::String error;                  // the transport's or the server's words
		public int64 retryAfterMs;                  // from a Retry-After header; 0 without one
		public Completion* completion;              // what arrived, merged
		public Core::String text;                   // the reply's prose: no <think> blocks, no text tool calls
		public Core::String thinking;
		public Core::Array<ToolCallDraft*> calls;   // native calls, or those the text wrote out
		public bool textCalls;                      // `calls` came from the text
		public bool unparsedCall;                   // the text opens a tool call that did not parse
		public int64 ttftMs;                        // -1 when no token came
		public int64 ms;
		public int64 keepalives;
	}

	// Runs `request`, watching `cancelled` (set from another thread) between reads.
	StepResult* RunStep(const StepRequest& request, const StepEvents& events, const bool* cancelled);
	const char* WireEndName(WireEnd end);
}
