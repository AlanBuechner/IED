// A turn running on a thread of its own (docs/12 "The loop"): each step sends the conversation to the group's primary
// model and streams the reply into segments; the reply is classified and acted on - tool calls run (permission check,
// approval, result), an answer ends the turn in Ask and Plan mode, finish ends it in Agent mode - and every stop
// reason gets its recovery (docs/12 "Recovery policy"): retries with backoff and the fallback models, continuing after
// a cut, nudges as harness notes, compression on a context overflow. A reason past its bound pauses the chat, never
// silently ends it. The thread holds the server lock only for what it reads and writes of the chat.

include "service.ch"
include "step.ch"
include "classify.ch"

namespace Ied
{
	const int64 kRunTransportAttempts = 4;
	const int64 kRunFallbackAfter = 2;
	const int64 kRunMostBackoffMs = 60000;
	const int64 kRunNoProgressBound = 3;
	const int64 kRunSameErrorBound = 3;
	const int64 kRunMalformedBound = 3;
	const int64 kRunKeptToolResultBytes = 16 * 1024;
	const int64 kRunCompressAtPercent = 85;

	class TurnRun
	{
		[operator("create")] public void Init() {}

		public Server* server;
		public Chats* chats;
		public Chat* chat;
		public ChatRun* run;
		public Turn* turn;
		public Workspace* ws;

		public void Execute();

		// --- run.cf: the loop ---
		private bool Prepare();
		private void Discover();
		private void Step();
		private void Handle(StepVerdict verdict, StepResult* result);
		private void Progress();
		private bool Nudge(const char* reason, int64 bound, Core::StringView note);
		private void NoProgress();
		private void Transport(StepVerdict verdict, StepResult* result);
		private void Overflow(StepResult* result);
		private void Retry(StepVerdict verdict, StepResult* result, int64 bound);
		private void Pause(Core::StringView reason, Core::StringView why);
		private void Finish(Core::StringView stop);
		private void End();
		private bool Sleep(int64 ms);

		// --- runstream.cf: requests and streaming ---
		private Core::String RequestBody();
		private StepRequest Request();
		private void OnEvent(StepEventKind kind, Core::StringView text, int64 call);
		private Segment* NewSegment(const char* type);
		private Segment* Begin(Segment* segment);
		private void Close(Segment* segment);
		private void CloseStreaming();
		private void Account(const StepResult* result, int64 promptEstimate);
		private void PublishContext(int64 used);
		private int64 Window() const;
		private int64 EstimateTokens() const;
		private bool Compress();
		// A note to the model, shown as a grey harness line; UiNote only shows it.
		private void HarnessNote(Core::StringView kind, Core::StringView text);
		private void UiNote(Core::StringView kind, Core::StringView text);
		private void DeliverSteering();
		private void SaveRunning();

		// --- runtools.cf: tool calls ---
		private void RecordAssistant(const StepResult* result, bool withCalls);
		private void ExecuteCalls(StepResult* result);
		private void ExecuteCall(ToolCallDraft* call, Segment* segment, Core::StringView modelId);
		private ToolOutcome RunCall(ToolSpec* spec, ToolCallDraft* call, Segment* segment, const Serialization::DomNode& args);
		private ToolOutcome RunMeta(ToolSpec* spec, Segment* segment, const Serialization::DomNode& args);
		private bool AwaitApproval(ToolSpec* spec, Segment* segment, const ToolPlan& plan, Core::StringView why, const Serialization::DomNode& args, ToolOutcome& refused);
		private ToolOutcome AskUser(Segment* segment, const Serialization::DomNode& args);
		private ToolOutcome SetTasks(const Serialization::DomNode& args);
		private ToolOutcome UpdateTask(const Serialization::DomNode& args);
		private ToolOutcome FinishCall(const Serialization::DomNode& args);
		private void ToolState(Segment* segment, const char* state);
		private void Resolve(Segment* segment, const ToolOutcome& outcome, int64 ms);
		private void TrackLoop(ToolCallDraft* call, const ToolOutcome& outcome);
		private void TrackModified(Core::StringView path, int64 plus, int64 minus);

		// The models to try in order, the one in use, and the attempts on it.
		private Core::Array<ModelProfile*> candidates;
		private int64 candidate;
		private RoleModel role;
		private Core::String system;
		// The core tools and the workspace's plugins' tools, as the turn started.
		private ToolRegistry* tools;
		private PermissionRules rules;
		// ai.maxSteps; 0 is no limit.
		private int64 maxSteps;
		private int64 steps;
		private int64 attempts;
		// Recovery counts of the step in progress, by stop reason; reset when a step makes progress.
		private Core::HashMap<Core::String, int64> counts;
		private int64 noProgress;
		private Core::String lastCallKey;
		private int64 sameErrors;
		private int64 malformed;
		private bool finishNudged;
		// How the turn ends: set once, then the loop stops.
		private bool ending;
		private Core::String stop;
		private bool pausing;
		private Core::String pauseWhy;
		// The step's streaming segments: the open thinking and text segments, and one tool segment per native call.
		private Segment* thinking;
		private Segment* text;
		private Core::Array<Segment*> callSegments;
		private int64 openedMs;
	}

	// Starts `chat`'s current turn on a thread of its own; the lock is held.
	void StartRun(Server* server, Chats* chats, Chat* chat, Workspace* ws);
}
