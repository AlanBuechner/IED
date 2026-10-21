// A chat as the server keeps it (docs/04, docs/10 "Chats", docs/12): its settings, its turns - each the segments the
// clients show and the messages the model is sent - its task list, the messages queued for the next turn, and what it
// waits on: approvals and a question. Everything here is written to JSON the way docs/10 shows it.

include "api/plugin.ch"

namespace Ai
{
	enum class ChatMode { Ask, Plan, Agent }
	enum class PermissionMode { Plan, Ask, AcceptEdits, Auto }
	enum class ChatState { Idle, Running, AwaitingApproval, AwaitingAnswer, Paused }

	const char* ChatModeName(ChatMode mode);
	bool ReadChatMode(Core::StringView text, ChatMode& mode);
	const char* PermissionModeName(PermissionMode mode);
	bool ReadPermissionMode(Core::StringView text, PermissionMode& mode);
	const char* ChatStateName(ChatState state);
	// The permission a mode starts with (docs/11): Plan's is plan, the others ask.
	PermissionMode DefaultPermission(ChatMode mode);

	// One piece of a turn the clients show: "text", "thinking", "tool", "harness" or "steering".
	class Segment
	{
		public Core::String segId;
		public Core::String type;
		public Core::String text;          // text, thinking, harness and steering segments
		public bool done;
		public Core::String model;
		public Core::String kind;          // a harness note's kind ("continue_after_max_tokens")
		// A tool segment:
		public Core::String tool;
		public Core::String callId;
		public Core::String argsText;
		public Core::String state;         // running, awaiting_approval, ok, error, denied, interrupted
		public Core::String preview;       // the result as the card shows it
		public bool isError;
		public int64 ms;
		public int64 plus;                 // lines a write added and removed
		public int64 minus;
		public Core::String source;        // where the tool ran ("built-in", "Output: AI")
		public Core::Array<IED::ToolImage> images;   // what the result showed the model, for the card
		public int64 tokens;

		public void WriteJson(IED::JsonWriter& json) const;
		// The members WriteJson writes, without the braces.
		public void WriteFields(IED::JsonWriter& json) const;
		public void Read(const Serialization::DomNode& node);
	}

	// `images` as the chat keeps them and the card gets them: [{mime, path}].
	void WriteToolImages(IED::JsonWriter& json, const Core::Array<IED::ToolImage>& images);

	// One message as the model is sent it: role "user", "assistant" or "tool".
	class ModelMessage
	{
		public Core::String role;
		public Core::String content;
		public Core::String toolCalls;     // an assistant's calls, the JSON array of the request format; empty for none
		public Core::String toolCallId;    // a tool result's call
		public Core::Array<IED::ToolImage> images;   // a tool result's images (images.ch)

		// As the chat keeps it.
		public void WriteJson(IED::JsonWriter& json) const;
		// As a request sends it; with `unsent` (why its images are not sent, images.ch), the images named in its text.
		public void WriteRequest(IED::JsonWriter& json, Core::StringView unsent) const;
		public void Read(const Serialization::DomNode& node);
	}

	class Turn
	{
		public Core::String turnId;
		public Core::String userText;
		public Core::String fromChatId;               // another chat sent the message (chattools.ch); empty: the owner
		public Core::String fromTitle;
		public int64 hops;                            // chats in a row that messaged each other up to this turn
		public ChatMode mode;
		public Core::String model;
		public Core::String group;
		public Core::Array<Segment*> segments;
		public Core::String stop;                     // empty while it runs
		public Core::Array<Core::String> recoveries;  // what the harness did, in order ("continued after max_tokens")
		public int64 tokensIn;                        // the last request's prompt
		public int64 tokensOut;                       // every request's completion, summed
		public int64 ttftMs;                          // the last request's time to first token
		public int64 genMs;                           // time spent generating after the first token, summed
		public int64 startedMs;
		public int64 endedMs;
		public Core::String prefill;                  // while a request's prompt is read: its server's progress, JSON
		public Core::Array<ModelMessage*> messages;   // what the model is sent of this turn, in order
		public bool compressed;                       // left out of requests to fit the context (kept on disk)

		public Segment* FindSegment(Core::StringView segId) const;
		public Segment* ToolSegment(Core::StringView callId) const;
		// The clients' view; with `messages` the stored form, the model's messages too.
		public void WriteJson(IED::JsonWriter& json, bool withMessages) const;
		public void Read(const Serialization::DomNode& node);
	}

	class ModifiedFile
	{
		public Core::String path;
		public int64 plus;
		public int64 minus;
	}

	// A tab a chat uses that another plugin opened for it (docs/04 "What a chat has open"): a page the AI drives, ...
	class ChatResource
	{
		public Core::String ref;      // the tab ("browser:pg-3")
		public Core::String title;
		public Core::String kind;     // "page", ...: the side panel's icon
	}

	class ChatTask
	{
		public Core::String id;
		public Core::String title;
		public Core::String status;        // pending, in_progress, done, skipped
		public Core::String note;
	}

	class Approval
	{
		public Core::String approvalId;
		public Core::String callId;
		public Core::String tool;
		public Core::String args;          // JSON
		public Core::String summary;       // what the card says it does
		public Core::String why;           // the mode and rule that made it ask
		public Core::String subject;       // the rule "allow for this chat" adds: "fs.write(src/**)"
		public Core::String diff;          // a write's change, unified
		public int64 plus;
		public int64 minus;
		public Core::String decision;      // empty until answered: allow, deny, always, withdrawn
		public Core::String message;       // a denial's reason
		public Core::String by;

		public void WriteJson(IED::JsonWriter& json) const;
	}

	class Question
	{
		public Core::String questionId;
		public Core::String callId;
		public Core::String question;
		public Core::Array<Core::String> options;
		public bool allowText;
		public bool answered;
		public Core::String answer;

		public void WriteJson(IED::JsonWriter& json) const;
	}

	// A turn that runs: what its thread and the chat.* methods share under the server lock. `cancelled` is also read
	// by the step's reads without it.
	class ChatRun
	{
		public Turn* turn;
		public bool cancelled;
		public Threading::ConditionVariable wake;   // an approval or answer came, or the run was stopped
	}

	class Chat
	{
		public Core::String chatId;
		public Core::String wsId;
		public Core::String title;
		public ChatMode mode;
		public PermissionMode permission;
		public Core::String group;
		public ChatState state;
		public Core::String stateReason;              // why it paused
		public int64 createdMs;
		public int64 updatedMs;
		public Core::Array<Turn*> turns;
		public Core::Array<ChatTask*> tasks;
		public Core::Array<Core::String> queued;      // sent while a turn ran, for the next turn
		public Core::Array<Core::String> injected;    // for the running turn's next step
		public Core::Array<Approval*> approvals;       // waiting for a decision
		public Question* question;                    // the ask_user question waiting, or null
		public Core::Array<Core::String> allowed;     // rules the owner allowed for this chat ("Allow for this chat")
		public Core::Array<ModifiedFile*> modified;   // what the AI wrote, with its lines added and removed
		public Core::Array<Core::String> loadedTools; // deferred tools tool_search or a call loaded (docs/13)
		public Core::Array<ChatResource*> resources;  // tabs it uses that plugins reported (chat.resource)
		public Core::String context;                  // the last context.changed data (JSON); empty before a request
		public ChatRun* run;                          // the running turn, or null
		public int64 counter;

		public Turn* Current() const { return turns.IsEmpty() ? nullptr : turns[turns.Count() - 1]; }
		public Core::String NextId(const char* prefix);
		public Core::String Topic() const { return Core::String("chat:") + chatId.View(); }
		public Core::String SummaryJson() const;
		public int64 WaitingApprovals() const;
		public void WriteSummary(IED::JsonWriter& json) const;
		public void WriteTasks(IED::JsonWriter& json) const;
		public void WriteQueued(IED::JsonWriter& json) const;
		public void WriteModified(IED::JsonWriter& json) const;
		public void WriteResources(IED::JsonWriter& json) const;
		// The chat:<chatId> snapshot: the summary, the model and group in use, tasks, queued messages, the last `recent` turns
		// and what it waits on.
		public Core::String SnapshotJson(Core::StringView model, Core::StringView groupInUse, int64 recent) const;
	}

	int64 ChatNowMs();
	// A workspace's id as the API's calls take it.
	Core::StringView WsId(const IED::Workspace& ws);
}
