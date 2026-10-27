// A turn running on a thread of its own (docs/12 "The loop"): each step sends the conversation to the group's primary
// model and streams the reply into segments; the reply is classified and acted on - tool calls run (permission check,
// approval, result), an answer ends the turn in Ask and Plan mode, finish ends it in Agent mode - and every stop
// reason gets its recovery (docs/12 "Recovery policy"): retries with backoff and the fallback models, continuing after
// a cut, nudges as harness notes, compression on a context overflow. A reason past its bound pauses the chat, never
// silently ends it. The thread holds the server lock only for what it reads and writes of the chat.

include "service.ch"
include "step.ch"
include "classify.ch"

namespace Ai
{
	const int64 kRunTransportAttempts = 4;
	const int64 kRunFallbackAfter = 2;
	const int64 kRunMostBackoffMs = 60000;
	const int64 kRunNoProgressBound = 3;
	const int64 kRunSameErrorBound = 3;
	const int64 kRunMalformedBound = 3;
	const int64 kRunKeptToolResultBytes = 16 * 1024;
	const int64 kRunProgressEveryMs = 250;   // turn.progress at most this often
	const int64 kRunEstimateShownMs = 3000;  // a prompt estimated to take this long to read shows the estimate
	const int64 kRunProbeRetryMs = 60000;    // a model's server that did not answer discovery is asked again after this

	class TurnRun
	{
		[operator("create")] public void Init() {}

		public IED::PluginContext* context;
		public Chats* chats;
		public Chat* chat;
		public ChatRun* run;
		public Turn* turn;
		public IED::Workspace ws;

		public void Execute();

		// --- run.cf: the loop ---
		private bool Prepare();
		private void ResolveMessage();
		// What a message's mentions name, read now, and its attachments, as words added to it (mentions.ch); its images in
		// `images`. The lock is not held.
		private Core::String MessageExtras(const Core::Array<Mention>& mentions, const Core::Array<Attachment>& attachments, Core::Array<IED::ToolImage>& images);
		private void Discover();
		private void Step();
		private void Handle(StepVerdict verdict, StepResult* result);
		private void Progress();
		// A prefill that ran out its watchdog: its wait, doubled, is the floor under the retries' deadline for the rest of
		// the turn, so a retry is never given the deadline that just failed (docs/12 "Watchdogs").
		private void PrefillRanShort(const StepResult* result);
		private bool Nudge(const char* reason, int64 bound, Core::StringView note);
		private void NoProgress();
		private void Transport(StepVerdict verdict, StepResult* result);
		private void Overflow(StepResult* result);
		private void Retry(StepVerdict verdict, StepResult* result, int64 bound);
		// Why a step cut with nothing to continue from pauses: a stalled prefill names the watchdog's own numbers (docs/12
		// "Watchdogs"), the usual cause being a server that reads the prompt slower than the rate measured on earlier steps.
		private Core::String StallWhy(Core::StringView reason, const StepResult* result);
		private void Pause(Core::StringView reason, Core::StringView why);
		private void Finish(Core::StringView stop);
		// The turn's code let an exception escape (`message`: an RpcError's, else empty; `stack`: where it was thrown):
		// the turn ends with harness_error, saying so, and the server's log has the rest.
		private void Failed(Core::StringView message, Core::StringView stack);
		private void End();
		private bool Sleep(int64 ms);

		// --- runstream.cf: requests and streaming ---
		// The request's JSON; `promptTokens` its estimate, images counted as images.
		private Core::String RequestBody(int64& promptTokens);
		private StepRequest Request();
		// Of a request of `promptTokens` whose system message and tools hash to `head`, what its server reads: what the chat
		// added since the last request when nothing in front of it changed, else all of it.
		private int64 ExpectedRead(int64 promptTokens, int64 head) const;
		private void OnEvent(StepEventKind kind, Core::StringView text, int64 call);
		// The prompt read so far (`progress` llama.cpp's object; empty: reading ended), for the clients.
		private void Prefill(Core::StringView progress);
		// For a server that does not report its reading, how long it should take, when that is a while.
		private void EstimatePrefill(const StepRequest& request);
		private Segment* NewSegment(const char* type);
		private Segment* Begin(Segment* segment);
		private void Close(Segment* segment);
		private void CloseStreaming();
		// The step's counts: its prompt's tokens (the server's, else `promptEstimate`, else the calibrated estimate) and its
		// output on turn.stats and the context meter; `estimateBefore` is EstimateTokens() when its request was made.
		private void Account(const StepResult* result, int64 promptEstimate, int64 estimateBefore);
		private void PublishContext(int64 used);
		private int64 Window() const;
		// IED's estimate of the context (its bytes at kContextBytesPerToken), and that estimate corrected by how far it was
		// from the last count the model's server gave.
		private int64 EstimateTokens() const;
		private int64 CalibratedTokens() const;
		private Core::String SentTools() const;
		private bool Compress();
		// A note to the model, shown as a grey harness line; UiNote only shows it.
		private void HarnessNote(Core::StringView kind, Core::StringView text);
		private void UiNote(Core::StringView kind, Core::StringView text);
		// The messages sent while the turn ran, given to the model as plain messages and shown where it got them (docs/12
		// "Messages while a turn runs"); with `inResult` they go with a tool result (Claude mid-query), each saying it is
		// the owner's. The lock is not held.
		private void DeliverMessages(bool inResult);
		private void SaveRunning();

		// --- runtools.cf: tool calls ---
		private void RecordAssistant(const StepResult* result, bool withCalls);
		private void ExecuteCalls(StepResult* result);
		// `last`: the step's last call, whose result to Claude carries what came for the turn meanwhile.
		private void ExecuteCall(ToolCallDraft* call, Segment* segment, Core::StringView modelId, bool last);
		// The chat's clips pasted into `args` (clipboard.ch); false with `missing` a name it has no clip of.
		private bool Pasted(Serialization::DomNode& args, Core::String& missing);
		// What the model is told when a call pastes a clip there is not.
		private ToolOutcome UnknownClip(Core::StringView tool, Core::StringView missing);
		private ToolOutcome RunCall(ToolSpec* spec, ToolCallDraft* call, Segment* segment, const Serialization::DomNode& args);
		private ToolOutcome RunMeta(ToolSpec* spec, Segment* segment, const Serialization::DomNode& args);
		private bool AwaitApproval(ToolSpec* spec, Segment* segment, const ToolPlan& plan, Core::StringView why, const Serialization::DomNode& args, ToolOutcome& refused);
		private ToolOutcome AskUser(Segment* segment, const Serialization::DomNode& args);
		private ToolOutcome SetTasks(const Serialization::DomNode& args);
		private ToolOutcome UpdateTask(const Serialization::DomNode& args);
		private ToolOutcome FinishCall(const Serialization::DomNode& args);
		// tool_search: the deferred tools the query names loaded for the chat, their definitions told the model.
		private ToolOutcome SearchTools(const Serialization::DomNode& args);
		// The deferred tool loaded for the chat, inside the host lock.
		private void LoadTool(const ToolSpec* spec);
		private void ToolState(Segment* segment, const char* state);
		private void Resolve(Segment* segment, const ToolOutcome& outcome, int64 ms);
		private void TrackLoop(ToolCallDraft* call, const ToolOutcome& outcome);
		private void TrackModified(Core::StringView path, int64 plus, int64 minus);

		// --- runclaude.cf: Claude through the owner's CLI (docs/04 "The Claude Code provider") ---
		// The candidates in the order the plan's usage leaves them (docs/04 "Pacing"): a Claude model over its place's pace
		// after the rest, a used-up one left out; why the last went, when every candidate did. The lock is held.
		private Core::String ChooseClaude();
		// The model the candidate in use runs on Claude.
		private Core::String ClaudeModel() const;
		// One step: what the session has not seen sent, the CLI's stream read to the response's end.
		private StepResult* ClaudeStep(const StepEvents& events);
		private bool StartClaude(Core::String& problem);
		private void SendToClaude();
		// The turn's messages the session has not seen, set aside for the next tool result while a query runs. The lock is
		// held.
		private void SetAside();
		// One of the step's lines; true when the step is over.
		private bool TakeClaudeLine(ClaudeLine* line, ClaudeResponse* response, StepResult* result, const StepEvents& events, int64 startedMs);
		// A line beside a step's: a rate limit event, the session's id, a query's result.
		private void ClaudeAside(ClaudeLine* line);
		private void ClaudeErrorOf(ClaudeLine* line, StepResult* result);
		// The query's result: the context window the CLI reports.
		private void ClaudeResult(ClaudeLine* line);
		// The CLI's own count of the session's context and its window (get_context_usage) as the chat's meter, split into
		// IED's parts; the window learned.
		private void ClaudeContext(const Serialization::DomNode& usage);
		// Reads the query to its result.
		private void DrainClaude();
		// Claude cannot serve the turn (`claudeBlocked`: a usage limit, no subscription sign-in): the next candidate that is
		// not Claude - or after a usage limit a Claude one with room left (Fable, metered apart) - or a pause saying why. The
		// lock is held.
		private void ClaudeBlocked();
		// The call's result to the CLI's tools/call for it; the step's `last` call's carries the messages that came meanwhile.
		private void AnswerClaude(Core::StringView toolUseId, const ToolOutcome& outcome, bool last);
		// The turn is over: the query settled, the usage asked, the session kept for the chat's next turn.
		private void EndClaude();
		private ClaudeSession* claude;                               // the chat's session once a Claude step used it
		private int64 claudeSent;                                    // the turn's messages before this the session has
		private Core::String claudeAside;                            // messages that came mid-query, for the next tool result
		private Core::Array<IED::ToolImage> claudeAsideImages;       // and their images
		private Core::String claudeBlocked;                          // why Claude cannot serve this turn; empty while it can
		private Core::String claudeBlockedStop;                      // the stop a pause for it records: usage_limit, not_subscription

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
		private int64 prefillFloorMs;   // the least a prefill watchdog allows this turn, after one ran short
		private int64 requestHead;      // the step's request's system message and tools, hashed (ExpectedRead)
		private int64 requestRead;      // what its server was expected to read of it
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
		private int64 progressMs;   // when turn.progress last went out
	}

	// Starts `chat`'s current turn on a thread of its own; inside the host lock.
	void StartRun(Chats* chats, Chat* chat, const IED::Workspace& ws);
	// The tools a turn of `chat` gets: the core set, the workspace's plugins' (their problems in `contributed`) and the
	// chat's own - chats, clipboard, models, background tasks, agents. Inside the host lock.
	ToolRegistry* TurnTools(Chats* chats, Chat* chat, const IED::Workspace& ws, Core::Array<Core::String>& contributed);
	// The system message a turn of `chat` whose message is `userText` is sent (docs/14). Inside the host lock.
	Core::String TurnSystem(Chats* chats, Chat* chat, const IED::Workspace& ws, ToolRegistry* tools, Core::StringView userText, const Core::Array<ModelProfile*>& models,
		Core::Array<Core::String>& contributed);
	// The tool definitions a request sends: a local model the chat's loaded ones, Claude's session every tool.
	Core::String ToolsSent(const ToolRegistry* tools, ChatMode mode, bool claude);
	// What fills a chat's context, estimated (bytes / 4): the system message and tool definitions of `systemBytes` and
	// `toolsBytes`, its messages and its tool results, the turns compression left out not counted.
	Core::Array<ContextPart> ContextParts(const Chat* chat, int64 systemBytes, int64 toolsBytes);
	// The context meter of a chat with turns but none kept - an older server ran it, or its count was lost: its last
	// turn's prompt as its model's server counted it (`tokensIn`), split like what its next turn would send; that send
	// estimated when no turn kept a count. For the model its next turn starts on. Inside the host lock.
	Core::String MeterFromTurns(Chats* chats, Chat* chat, const IED::Workspace& ws);
	// The chat's meter with its count, window and model kept and its parts made IED's four again, split across the
	// count (a meter a build kept with the Claude CLI's own rows). Inside the host lock.
	Core::String ResplitContext(Chats* chats, Chat* chat, const IED::Workspace& ws);
}
