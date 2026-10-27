// One model request of a run (docs/12 "The loop": a step): the streamed POST read under the watchdogs - the prefill
// deadline until the first token (keepalive comments extend it, and so does the server saying it still has the work in
// its engine when the deadline comes: `ServerSaysWorking`), the idle one between chunks after it, a hard cap, a
// reply caught repeating one passage (Repetition.ch) -
// and what arrived put together: reasoning from its fields and <think> blocks, text with any tool calls written as text
// taken out of it, native tool calls merged by id then index. Text is handed out as it streams, a short tail held back
// so a tool-call opener cut across chunks never shows as prose.

include "Completion.ch"

namespace Ai
{
	// How the request ended on the wire (docs/12 taxonomy, the transport and stall rows).
	enum class WireEnd { Completed, ConnectFailed, HttpError, Disconnected, StalledPrefill, StalledStream, StepCap, Cancelled, Repetition }

	enum class StepEventKind { Thinking, Text, CallStarted, CallArguments, Progress, Working }

	// What streams out of a step as it happens: thinking and text deltas, a native call starting (`text` its name, `call`
	// its index in the step's calls) and its argument deltas, before the first token the prompt read so far (`text`
	// llama.cpp's prompt_progress object) and, when the prefill watchdog asked and the server said it is still reading,
	// a Working event whose `text` says so. Called on the step's thread.
	alias StepEvents = func<void(StepEventKind kind, Core::StringView text, int64 call)>;

	class StepRequest
	{
		public Core::String m_Url;
		public Core::Array<Core::String> m_Headers;   // "Name: value"
		public Core::String m_Body;                   // the chat completion request, JSON
		public int64 m_PromptTokens;                  // the body's estimate
		public int64 m_ReadTokens;                    // of those, what the server is expected to read: past its cached prefix
		public int64 m_ReadMs;                        // how long the server takes to read the whole prompt, from the measured rate
		public int32 m_ConnectMs;
		public int64 m_PrefillMs;                     // until the first token
		public int64 m_PrefillExtendMs;               // what a keepalive comment adds
		public int64 m_IdleMs;
		public int64 m_CapMs;
		public Core::String m_ServerRoot;   // the model server's root for the liveness endpoints beside the OpenAI ones; empty when unknown
	}

	class StepResult
	{
		[operator("create")] public void Init() { m_Completion = new Completion(); m_TtftMs = -1; }

		public WireEnd m_End;
		public int32 m_HttpStatus;
		public Core::String m_Error;                  // the transport's or the server's words
		public int64 m_RetryAfterMs;                  // from a Retry-After header; 0 without one
		public Completion* m_Completion;              // what arrived, merged
		public Core::String m_Text;                   // the reply's prose: no <think> blocks, no text tool calls
		public Core::String m_Thinking;
		public Core::Array<ToolCallDraft*> m_Calls;   // native calls, or those the text wrote out
		public bool m_TextCalls;                      // `calls` came from the text
		public bool m_UnparsedCall;                   // the text opens a tool call that did not parse
		public int64 m_TtftMs;                        // -1 when no token came
		public int64 m_Ms;
		public int64 m_Keepalives;
		// What the prefill watchdog allowed and what it waited, and what the request was sized with: for the pause after a
		// stall to name its own numbers (docs/12 "Watchdogs"). 0 / -1 until one has run.
		public int64 m_PrefillBudgetMs;
		public int64 m_PrefillWaitMs;                 // how long it waited on the server before the deadline
		public int64 m_PromptTokens;
		public double m_PrefillRate;
		public int64 m_PrefillConfirms;               // times the server said it was still reading when the deadline came
	}

	// What a Prometheus answer says of the server: whether it has the vLLM request gauges at all
	// (MetricsHasRequestGauges: what makes it worth asking) and whether one of them puts work in the engine
	// (MetricsSayWorking). `Step.cf`'s liveness question for the prefill watchdog.
	bool MetricsHasRequestGauges(Core::StringView text);
	bool MetricsSayWorking(Core::StringView text);

	// Runs `request`, watching `cancelled` (set from another thread) between reads.
	StepResult* RunStep(const StepRequest& request, const StepEvents& events, const bool* cancelled);
	const char* WireEndName(WireEnd end);
}
