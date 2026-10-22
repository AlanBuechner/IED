// Claude through the owner's Claude Code CLI (docs/04 "The Claude Code provider"), the parts that are only text: the plan's
// usage windows from the CLI's get_usage answers and rate_limit_events, each window's pace and the model pacing picks from
// a ladder, a response put together from the CLI's stream events, the errors it reports, and the lines IED writes to it -
// control requests and answers, user messages, and its in-process MCP server's replies.

include "chat.ch"
include "completion.ch"

namespace Ai
{
	const int64 kSessionWindowMs = 5 * 3600 * 1000;
	const int64 kWeekWindowMs = 7 * 24 * 3600 * 1000;
	const char* kClaudeToolPrefix = "mcp__ied__";
	const char* kClaudeServerName = "ied";
	const int64 kHistoryTextBytes = 48 * 1024;   // of the earlier turns a new session is told

	// One window of the plan's limits.
	class UsageWindow
	{
		public Core::String id;          // five_hour, seven_day, seven_day_opus, seven_day_sonnet, model:<name>
		public Core::String label;       // "Session (5 h)", "Week", "Opus week"
		public Core::String model;       // a per-model window's model in lower case ("opus", "fable"); empty for the shared ones
		public double utilization;       // percent used, 0-100 (past 100 when usage ran over)
		public int64 resetsAtMs;         // 0 when unknown
		public int64 lengthMs;
	}

	class ClaudeUsage
	{
		public bool known;               // the CLI answered
		public bool available;           // the plan's limits apply: signed in with a subscription
		public Core::String plan;        // pro, max, team, enterprise; empty for an API key or when unknown
		public Core::Array<UsageWindow*> windows;
		public Core::String status;      // the last rate_limit_event's: allowed, allowed_warning, rejected
		public Core::String limitedBy;   // the window it named
		public int64 limitResetsAtMs;
		public bool extraEnabled;        // extra usage past the plan
		public double extraUtilization;  // percent of its monthly limit; -1 when unknown
		public int64 updatedMs;
		public Core::String error;       // why the last ask failed; empty when it did not

		public UsageWindow* Find(Core::StringView id) const;
		// The window `id`, added when it is not there yet.
		public UsageWindow* Window(Core::StringView id);
		// Nothing known any more: the CLI signed out.
		public void Forget();
	}

	// get_usage's answer into `usage`, its windows replacing those before; false when it is not one.
	bool ReadUsage(const Serialization::DomNode& response, ClaudeUsage& usage, int64 nowMs);
	// A rate_limit_event's rate_limit_info into `usage`: the windows it names (0-1 there) and its status.
	void TakeRateLimit(const Serialization::DomNode& info, ClaudeUsage& usage, int64 nowMs);
	// An ISO 8601 time ("2026-10-22T19:00:00.5+00:00", "...Z") as Unix milliseconds; 0 when it is not one.
	int64 IsoToMs(Core::StringView text);
	// How far through `window` is at `nowMs`, 0 to 1 (1 when its reset is unknown, 0 once it has reset).
	double Through(const UsageWindow& window, int64 nowMs);
	// The window's pace: its use against the even line that runs out at its reset (1.0); 0 early in a window that is
	// barely used, and once it has reset.
	double Pace(const UsageWindow& window, int64 nowMs);
	// Whether the window is used up and has not reset yet.
	bool UsedUp(const UsageWindow& window, int64 nowMs);
	// The weekly window of `model` alone: opus and sonnet their own, another a model_scoped one its name contains.
	UsageWindow* ModelWindow(const ClaudeUsage& usage, Core::StringView model);
	// {known, available, plan, status, limitedBy, limitResetsAtMs, extra?, updatedMs, error?, windows: [{id, label, model?,
	// utilization, resetsAtMs, lengthMs, through, pace}]} - the clients' and the AI's view.
	void WriteUsage(IED::JsonWriter& json, const ClaudeUsage& usage, int64 nowMs);

	// The pace of `model` against `target` (session, week, both) now: the worst of the windows that pace it, `why` which
	// ("the week is at 1.85x pace"; docs/04 "Pacing"); 0 and empty when the usage is unknown or no window paces it.
	double PaceAgainst(const ClaudeUsage& usage, Core::StringView model, Core::StringView target, int64 nowMs, Core::String& why);
	// Whether `target` is one pacing knows.
	bool IsPacingTarget(Core::StringView target);
	// When `atMs` comes, from `nowMs`, as a note says it: "in 2 h 14 min", "in 35 min", "in 3 d 4 h", "now".
	Core::String ResetText(int64 atMs, int64 nowMs);

	enum class ClaudeDeltaKind { Text, Thinking, CallStarted, CallInput }

	// What one stream event added: text and thinking, a tool call starting (`text` its IED name) and its input's JSON.
	struct ClaudeDelta
	{
		public ClaudeDeltaKind kind;
		public Core::String text;
		public int64 call;               // the call's index in the response's calls
	}

	// One model response from the CLI's stream_events (the Messages API's raw events).
	class ClaudeResponse
	{
		[operator("create")] public void Init() {}

		public Core::String model;
		public Core::String text;
		public Core::String thinking;
		public Core::Array<ToolCallDraft*> calls;   // `id` the tool_use id, `name` IED's, `arguments` the input's JSON
		public Core::String stopReason;             // tool_use, end_turn, max_tokens, refusal; empty before message_delta
		public bool started;                        // message_start came
		public bool stopped;                        // message_stop came
		public int64 promptTokens;                  // input, cache read and cache written
		public int64 cachedTokens;                  // of them, read from the cache
		public int64 outputTokens;

		// One stream_event's `event`; `deltas` gets what it added.
		public void Take(const Serialization::DomNode& event, Core::Array<ClaudeDelta>& deltas);

		private Core::HashMap<int64, int64> callOf;      // content block -> call
		private Core::HashMap<int64, bool> thinkingOf;   // content block -> a thinking block
	}

	enum class ClaudeError { None, UsageLimit, SignIn, Transport, Refused, Other }

	// What an assistant message's `error` means for the run.
	ClaudeError ClaudeErrorKind(Core::StringView error);
	// The reset a usage-limit message names ("...|1761159600", "resets 3pm" is not read); 0 when it names none.
	int64 LimitResetIn(Core::StringView text);

	// An image for the CLI, its bytes base64.
	class ClaudeImage
	{
		public Core::String mime;
		public Core::String base64;
	}

	Core::String ControlRequestLine(Core::StringView requestId, Core::StringView request);
	Core::String ControlAnswerLine(Core::StringView requestId, Core::StringView response);
	Core::String ControlRefusalLine(Core::StringView requestId, Core::StringView error);
	// A stream-json user message: its text, then its images.
	Core::String UserMessageLine(Core::StringView text, const Core::Array<ClaudeImage>& images);
	// initialize, naming IED's in-process MCP server.
	Core::String InitializeRequest();
	Core::String McpReply(const Serialization::DomNode& id, Core::StringView result);
	Core::String McpFailure(const Serialization::DomNode& id, int64 code, Core::StringView message);
	Core::String McpInitializeResult(Core::StringView protocolVersion);
	// tools/list's result from a request's "tools" array (the chat completion format), tool_search left out.
	Core::String McpToolsResult(Core::StringView requestTools);
	Core::String McpCallResult(Core::StringView text, const Core::Array<ClaudeImage>& images, bool isError);
	// IED's name for a tool the model names (mcp__ied__fs_read -> fs_read); others as they are.
	Core::StringView IedToolName(Core::StringView claudeName);
	// ai.models and ai.groups (their JSON) with Claude set up (docs/04 "Setting it up"): the Claude profiles added - claude-opus,
	// claude-sonnet, claude-haiku, claude-fable; one already there of a name kept as it is - and a `claude` group unless one is
	// there, its primary chain stepping down as the plan runs ahead of pace: claude-opus (1.0), claude-sonnet (1.25),
	// claude-haiku (1.5), then the default group's primary when that is not Claude (else the first model that is not);
	// `defaultGroup` the ai.defaultGroup value (empty: the first group).
	void WithClaudeModels(Core::StringView models, Core::StringView groups, Core::StringView defaultGroup, Core::String& modelsOut, Core::String& groupsOut);
	// Whether the ai.models value (JSON) names a claude-code profile.
	bool HasClaudeModel(Core::StringView models);
	// ai.models and ai.groups without the first pacing design's `model: "auto"` profiles (docs/04 "Pacing"): each role that
	// named one gets its ladder as paced places instead - 1.0, 1.25, 1.5 down the ladder against its target - and a rung
	// without a claude-code profile of its model gets claude-<model>. False, and nothing written, when there is none.
	bool WithoutAutoProfiles(Core::StringView models, Core::StringView groups, Core::String& modelsOut, Core::String& groupsOut);
	// The chat's turns before a new session, written out for its first message: the owner's messages, the answers and the
	// tool calls with a line of each result, the newest kept when they pass kHistoryTextBytes. Empty when there are none.
	Core::String HistoryText(const Core::Array<Turn*>& turns);
}
