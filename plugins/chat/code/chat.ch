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
	// The permission a mode starts with (docs/11): Plan's is plan (read only), the others auto.
	PermissionMode DefaultPermission(ChatMode mode);

	// What an owner's message names besides its words (docs/04 "Attachments and @-mentions").
	class Mention
	{
		public Core::String kind;    // file, folder, symbol, terminal, output, problems, diff, chat
		public Core::String ref;     // the path, symbol, terminal or output source id, or chat id; empty for problems, diff
		public Core::String label;   // as the message writes it, after its @
	}

	// A file sent with a message, kept in the chat's media folder.
	class Attachment
	{
		public Core::String name;
		public Core::String path;    // workspace-relative
		public Core::String mime;
		public int64 bytes;
	}

	// One piece of a turn the clients show: "text", "thinking", "tool", "harness" or "message" (a message sent while the
	// turn ran, where the model got it; "steering" in chats from before 2026-10-23).
	class Segment
	{
		public Core::String segId;
		public Core::String type;
		public Core::String text;          // text, thinking, harness and message segments
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
		// A message segment:
		public Core::Array<Mention> mentions;
		public Core::Array<Attachment> attachments;
		public Core::String fromChatId;    // another chat sent it; empty: the owner
		public Core::String fromTitle;
		public int64 atMs;                 // when the model was given it

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
		public Core::String reasoning;     // an assistant's thinking in its step: sent back where the server keeps it
		// What compression sends in its place (docs/04 "Context management"), kept apart in the chat's context.json:
		// the content itself never changes. Empty: the content is sent.
		public Core::String shown;
		public bool imagesOut;             // compression left its images out of requests; the chat keeps them

		// The text a request sends: what compression shows, else the content.
		public Core::StringView SentText() const;
		// Whether a request sends its images.
		public bool SendsImages() const;
		// As the chat keeps it: the content whole, whatever compression shows of it.
		public void WriteJson(IED::JsonWriter& json) const;
		// As a request sends it; with `unsent` (why its images are not sent, images.ch), the images named in its text.
		// With `withReasoning`, its thinking as reasoning_content.
		public void WriteRequest(IED::JsonWriter& json, Core::StringView unsent, bool withReasoning = false) const;
		public void Read(const Serialization::DomNode& node);
	}

	// A message as it waits for a turn - in the queue, or for the running turn's next step: the owner's, or another
	// chat's (chattools.ch).
	class OwnerMessage
	{
		public Core::String text;
		public Core::Array<Mention> mentions;
		public Core::Array<Attachment> attachments;
		public Core::String fromChatId;    // the chat that sent it; empty: the owner
		public Core::String fromTitle;

		// {text, from?, mentions?, attachments?}, as the chat's header keeps it.
		public void WriteJson(IED::JsonWriter& json) const;
		// From that, or from the plain text an older header kept.
		public void Read(const Serialization::DomNode& node);
	}

	void WriteMentions(IED::JsonWriter& json, const Core::Array<Mention>& mentions);
	void WriteAttachments(IED::JsonWriter& json, const Core::Array<Attachment>& attachments);
	void ReadMentions(const Serialization::DomNode& list, Core::Array<Mention>& mentions);
	void ReadAttachments(const Serialization::DomNode& list, Core::Array<Attachment>& attachments);

	class Turn
	{
		public Core::String turnId;
		public Core::String userText;
		public Core::Array<Mention> mentions;         // what the message names, read when the turn starts
		public Core::Array<Attachment> attachments;   // what was sent with it
		public Core::String fromChatId;               // another chat sent the message (chattools.ch); empty: the owner
		public Core::String fromTitle;
		public int64 hops;                            // chats in a row that messaged each other up to this turn
		public Core::String resumed;                  // IED started it after a restart (resume.ch): "relaunch" or "interrupted"
		public Core::String origin;                   // "background": IED started it to tell of background tasks that ended
		public ChatMode mode;
		public Core::String model;
		public Core::String group;
		public Core::String effort;                   // the last request's (docs/04 "Effort"); empty: none was sent
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
		// The owner's message as the clients get it: {text, from?, resumed?, mentions?, attachments?}.
		public void WriteUser(IED::JsonWriter& json) const;
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
		public Core::String close;    // the UI command that closes it for good (the side panel's Force close); "" for none
	}

	const int64 kClipShownBytes = 2048;   // of a clip's text, what the clients get

	// A task a chat's AI started in the background (docs/04 "Background tasks"): a command, a project's task, or a
	// sub-agent's chat. Its fields change under the server lock; its thread is the one that ends it.
	class BackgroundTask
	{
		public Core::String taskId;       // "bg-3"
		public Core::String kind;         // command, task, agent
		public Core::String title;        // "npm test", "build of Game", "agent reviewer"
		public Core::String state;        // running, done, failed, stopped, lost
		public int64 startedMs;
		public int64 endedMs;
		public int64 exitCode;
		public Core::String summary;      // how it ended, one line ("exit 0", "build succeeded in 40 s (0 errors)")
		public Core::String output;       // its output's end, or the agent's answer
		public Core::String agentChatId;  // an agent's chat
		public bool told;                 // its chat heard it ended
		public bool stopAsked;

		public bool Running() const { return state == "running"; }
		// {taskId, kind, title, state, startedMs, endedMs?, exitCode?, summary, chatId?, output?}; the output with `withOutput`.
		public void WriteJson(IED::JsonWriter& json, bool withOutput) const;
		public void Read(const Serialization::DomNode& node);
	}

	// A clip on the AI's clipboard (clipboard.ch): text it copied, pasted into later calls as {{clip:<name>}}.
	class Clip
	{
		public Core::String name;
		public Core::String text;
		public Core::String source;   // where it came from ("src/ring.cf lines 40-62", "the shell_run result")

		// Its lines; a final line break starts no line.
		public int64 Lines() const;
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
		public Core::String effort;                   // low, medium, high, xhigh or max; empty: its role's
		public Core::String pinnedModel;              // a model of ai.models over its group's primary; empty: the group's
		public Core::String claudeSession;            // the Claude Code session holding its conversation (docs/04)
		public Core::Array<Core::String> claudeTurns; // the turns that session has
		public ChatState state;
		public Core::String stateReason;              // why it paused
		public int64 createdMs;
		public int64 updatedMs;
		public Core::Array<Turn*> turns;
		public Core::Array<ChatTask*> tasks;
		public Core::Array<OwnerMessage*> queued;     // sent while a turn ran, for the next turn
		public Core::Array<OwnerMessage*> pending;    // sent while a turn ran, for its next step
		public Core::Array<Approval*> approvals;       // waiting for a decision
		public Question* question;                    // the ask_user question waiting, or null
		public Core::Array<Core::String> allowed;     // rules the owner allowed for this chat ("Allow for this chat")
		public Core::Array<ModifiedFile*> modified;   // what the AI wrote, with its lines added and removed
		public Core::Array<Core::String> loadedTools; // deferred tools tool_search or a call loaded (docs/13)
		public Core::Array<ChatResource*> resources;  // tabs it uses that plugins reported (chat.resource), this run only
		public Core::Array<Clip*> clips;              // the AI's clipboard, oldest first (clipboard.ch)
		public Core::Array<BackgroundTask*> background;   // its AI's background tasks, oldest first (background.ch)
		public Core::String agent;                    // the group's sub-agent this chat runs as; "" for an orchestrator
		public Core::String parentChatId;             // the chat that ran it as a sub-agent; ""
		public Core::String context;                  // the context meter (contextmeter.ch); empty before a request
		public int64 estimateOffset;                  // the last count a model's server gave less IED's estimate of that request
		public Core::String estimateModel;            // the model that count came from; "" before any (kept in chat.json)
		// What a model server's prefix cache holds of this chat (docs/12 "What makes a server read the whole prompt
		// again"), so a step's read is estimated as what was added rather than the whole prompt: the last request's count
		// (model `estimateModel`), its system message and tools as a hash, and whether a compression changed what
		// requests send since. Kept in chat.json.
		public int64 lastPromptTokens;
		public int64 promptHead;
		public bool promptRewritten;
		public bool claudeAsked;                      // Claude was asked to count its context this server run (claudecount.cf)
		public Core::String resume;                   // a call of this tool restarts the server: the chat goes on once it is back (resume.ch)
		public ChatRun* run;                          // the running turn, or null
		public int64 counter;

		public Turn* Current() const { return turns.IsEmpty() ? nullptr : turns[turns.Count() - 1]; }
		// It runs on a single model of its own, not a group (docs/04 "One choice per chat"): no group's effort applies.
		public bool RunsAlone() const { return !pinnedModel.IsEmpty() && group.IsEmpty() && agent.IsEmpty(); }
		public Core::String NextId(const char* prefix);
		public Core::String Topic() const { return Core::String("chat:") + chatId.View(); }
		public Core::String SummaryJson() const;
		public int64 WaitingApprovals() const;
		public void WriteSummary(IED::JsonWriter& json) const;
		public void WriteTasks(IED::JsonWriter& json) const;
		// The queued and the pending messages as the clients show them: their texts.
		public void WriteQueued(IED::JsonWriter& json) const;
		public void WritePending(IED::JsonWriter& json) const;
		public void WriteModified(IED::JsonWriter& json) const;
		public void WriteResources(IED::JsonWriter& json) const;
		// The background tasks as the clients show them: [{taskId, ..., output}].
		public void WriteBackground(IED::JsonWriter& json) const;
		public BackgroundTask* FindBackground(Core::StringView taskId) const;
		// The clips as the clients show them: [{name, source, lines, bytes, text}], each text cut to kClipShownBytes.
		public void WriteClips(IED::JsonWriter& json) const;
		// The chat:<chatId> snapshot: the summary, the model and group in use and its role's effort, tasks, queued messages,
		// the clips, the last `recent` turns and what it waits on.
		public Core::String SnapshotJson(Core::StringView model, Core::StringView groupInUse, Core::StringView roleEffort, int64 recent) const;
		// The chat topic's chat.changed: the summary, the model and group in use and its role's effort.
		public Core::String ChangedJson(Core::StringView model, Core::StringView groupInUse, Core::StringView roleEffort) const;
	}

	int64 ChatNowMs();
	// A workspace's id as the API's calls take it.
	Core::StringView WsId(const IED::Workspace& ws);
}
