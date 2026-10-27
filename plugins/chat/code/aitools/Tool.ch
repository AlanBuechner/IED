// The AI's tools (docs/13): each declares its name, description, JSON Schema, class (for permissions, docs/11) and
// whether it may run beside others, and has a planner - what a call would do, before the permission check: its
// subject, the card's summary and a write's diff - and a runner. Tools run on the chat's run thread without the
// server lock and take it for what they touch of the server's state. A tool with no runner (tasks_set, task_update,
// ask_user, finish) is the run's own (docs/12). A deferred tool (docs/13 "Loading tools on demand") is in every request's
// list from a chat's first turn as a stub - its first sentence, any arguments - and tool_search answers with its whole
// description and schema at the end of the conversation, so loading one never rewrites the front of the prompt a model
// server holds cached (docs/12 "Watchdogs").

include "chats/Chat.ch"
include "chats/Permissions.ch"

namespace Ai
{
	const int64 c_ToolIndexLineBytes = 120;   // a deferred tool's stub description
	const int64 c_ToolSearchMost = 5;         // the tools one tool_search shows

	// A call as a tool gets it.
	class ToolInvocation
	{
		public IED::PluginContext* m_Context;
		public IED::Workspace m_Ws;
		public Chat* m_Chat;
		public Core::String m_CallId;
		public Serialization::DomNode m_Args;
		public const bool* m_Cancelled;
		public Core::String m_Shown;      // what the tool shows workspace paths under, so they can be passed back: "ied:/"
	}

	class ToolOutcome
	{
		public Core::String m_Content;    // what the model is told
		public Core::String m_Preview;    // what the card shows; the content when empty
		public bool m_IsError;
		public bool m_Denied;             // refused by a rule or by the owner: it did not run
		public bool m_ReportsFailure;     // an error because what it ran failed (an exit code, a failed build), not the call
		public int64 m_Plus;              // lines a write added and removed
		public int64 m_Minus;
		public Core::String m_Source;     // where it ran, for the card ("built-in", "Output: AI")
		public int64 m_Ms;                // how long it ran, without the wait for an approval, when `timed`
		public bool m_Timed;
		public Core::Array<IED::ToolImage> m_Images;   // what the result shows the model (Images.ch)
	}

	// What a call would do: what the permission check and the approval card see.
	class ToolPlan
	{
		public Core::String m_Subject;    // "fs.write(src/a.cf)"
		public Core::String m_Summary;    // "Edit src/a.cf"
		public Core::String m_Diff;       // a write's change as a unified diff
		public int64 m_Plus;
		public int64 m_Minus;
		public Core::String m_Error;      // the call cannot work as asked: it fails without asking
	}

	alias ToolPlanner = func<ToolPlan(const ToolInvocation& call)>;
	// What a call that asks the owner adds to its plan for the card: a summary and diff in place of the plan's, or why
	// the call cannot work.
	alias ToolDetailer = func<void(const ToolInvocation& call, ToolPlan& plan)>;
	alias ToolRunner = func<ToolOutcome(const ToolInvocation& call)>;

	class ToolSpec
	{
		public Core::String m_Name;
		public Core::String m_Description;
		public Core::String m_Parameters;   // the JSON Schema of the arguments
		public ToolClass m_ToolClass;
		public bool m_ParallelSafe;
		public bool m_InPlan;               // offered in Plan mode (docs/13 "Mode presets")
		public bool m_AgentOnly;            // offered only in Agent mode (finish)
		public bool m_Deferred;             // listed by name until loaded for the chat
		public Core::String m_Group;        // "media", a plugin tool's group or else its plugin id
		public bool m_FolderRoots;          // works in a plugin's folder (plugins:/), which is no workspace: reads it from the disk
		public bool m_Asks;                 // every call asks until a rule allows it (a toolCalls entry's approval "ask")
		public ToolPlanner m_Plan;
		public ToolDetailer m_Detail;       // null: the card shows the plan as it is
		public ToolRunner m_Run;            // null: the run handles the call itself
	}

	class ToolRegistry
	{
		[operator("create")] public void Init() { m_Schemas = new Serialization::SchemaRegistry(); }

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

		public Core::Array<ToolSpec*> m_Tools;
		public Core::Array<Core::String> m_Problems;   // tools whose schema did not load
		private Serialization::SchemaRegistry* m_Schemas;
	}

	// A call's arguments read as JSON, repaired the ways models get it wrong (an empty string, a fence around it);
	// false when it still is not a JSON object.
	bool ReadToolArguments(Core::StringView text, Serialization::DomNode& args);
	// A tool error that teaches (docs/13 rule 4).
	ToolOutcome ToolError(Core::StringView message);
	ToolOutcome ToolResult(Core::StringView content, Core::StringView preview);
}
