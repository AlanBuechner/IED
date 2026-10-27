// The AI's tools (docs/13): each declares its name, description, JSON Schema, class (for permissions, docs/11) and
// whether it may run beside others, and has a planner - what a call would do, before the permission check: its
// subject, the card's summary and a write's diff - and a runner. Tools run on the chat's run thread without the
// server lock and take it for what they touch of the server's state. A tool with no runner (tasks_set, task_update,
// ask_user, finish) is the run's own (docs/12). A deferred tool (docs/13 "Loading tools on demand") is in every request's
// list from a chat's first turn as a stub - its first sentence, any arguments - and tool_search answers with its whole
// description and schema at the end of the conversation, so loading one never rewrites the front of the prompt a model
// server holds cached (docs/12 "Watchdogs").

include "chat.ch"
include "permissions.ch"

namespace Ai
{
	const int64 kToolIndexLineBytes = 120;   // a deferred tool's stub description
	const int64 kToolSearchMost = 5;         // the tools one tool_search shows

	// A call as a tool gets it.
	class ToolInvocation
	{
		public IED::PluginContext* context;
		public IED::Workspace ws;
		public Chat* chat;
		public Core::String callId;
		public Serialization::DomNode args;
		public const bool* cancelled;
	}

	class ToolOutcome
	{
		public Core::String content;    // what the model is told
		public Core::String preview;    // what the card shows; the content when empty
		public bool isError;
		public bool denied;             // refused by a rule or by the owner: it did not run
		public int64 plus;              // lines a write added and removed
		public int64 minus;
		public Core::String source;     // where it ran, for the card ("built-in", "Output: AI")
		public int64 ms;                // how long it ran, without the wait for an approval, when `timed`
		public bool timed;
		public Core::Array<IED::ToolImage> images;   // what the result shows the model (images.ch)
	}

	// What a call would do: what the permission check and the approval card see.
	class ToolPlan
	{
		public Core::String subject;    // "fs.write(src/a.cf)"
		public Core::String summary;    // "Edit src/a.cf"
		public Core::String diff;       // a write's change as a unified diff
		public int64 plus;
		public int64 minus;
		public Core::String error;      // the call cannot work as asked: it fails without asking
	}

	alias ToolPlanner = func<ToolPlan(const ToolInvocation& call)>;
	// What a call that asks the owner adds to its plan for the card: a summary and diff in place of the plan's, or why
	// the call cannot work.
	alias ToolDetailer = func<void(const ToolInvocation& call, ToolPlan& plan)>;
	alias ToolRunner = func<ToolOutcome(const ToolInvocation& call)>;

	class ToolSpec
	{
		public Core::String name;
		public Core::String description;
		public Core::String parameters;   // the JSON Schema of the arguments
		public ToolClass toolClass;
		public bool parallelSafe;
		public bool inPlan;               // offered in Plan mode (docs/13 "Mode presets")
		public bool agentOnly;            // offered only in Agent mode (finish)
		public bool deferred;             // listed by name until loaded for the chat
		public Core::String group;        // "media", or a plugin tool's plugin id
		public ToolPlanner plan;
		public ToolDetailer detail;       // null: the card shows the plan as it is
		public ToolRunner run;            // null: the run handles the call itself
	}

	class ToolRegistry
	{
		[operator("create")] public void Init() { schemas = new Serialization::SchemaRegistry(); }

		public void Add(ToolSpec* spec);
		public ToolSpec* Find(Core::StringView name) const;
		public bool Offered(const ToolSpec* spec, ChatMode mode) const;
		// The request's "tools" array for a chat in `mode`: every tool offered, a deferred one as its stub unless `whole`
		// (Claude takes every schema; the setting ai.tools.loadAll makes none deferred).
		public Core::String RequestTools(ChatMode mode, bool whole) const;
		// Whether a tool offered in `mode` is deferred.
		public bool HasDeferred(ChatMode mode) const;
		// The deferred tools that best match `query` - words, or "select:a,b" naming them - at most `most`.
		public Core::Array<ToolSpec*> SearchDeferred(Core::StringView query, ChatMode mode, int64 most) const;
		// Every deferred tool loaded: the setting ai.tools.loadAll.
		public void LoadAll();
		// The problems of `args` against the tool's schema, "<pointer>: <message>" each.
		public Core::Array<Core::String> Check(const ToolSpec* spec, const Serialization::DomNode& args) const;
		// The offered tools whose names look like `name`, best first.
		public Core::Array<Core::String> Suggest(Core::StringView name, ChatMode mode) const;

		public Core::Array<ToolSpec*> tools;
		public Core::Array<Core::String> problems;   // tools whose schema did not load
		private Serialization::SchemaRegistry* schemas;
	}

	// A call's arguments read as JSON, repaired the ways models get it wrong (an empty string, a fence around it);
	// false when it still is not a JSON object.
	bool ReadToolArguments(Core::StringView text, Serialization::DomNode& args);
	// A tool error that teaches (docs/13 rule 4).
	ToolOutcome ToolError(Core::StringView message);
	ToolOutcome ToolResult(Core::StringView content, Core::StringView preview);
}
