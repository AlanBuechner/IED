// A chat as the server keeps it (docs/04, docs/10 "Chats", docs/12): its settings, its turns - each the segments the
// clients show and the messages the model is sent - its task list, the messages queued for the next turn, and what it
// waits on: approvals and a question. Everything here is written to JSON the way docs/10 shows it.

include "api/Plugin.ch"

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
		public Core::String m_Kind;    // file, folder, symbol, terminal, output, problems, diff, chat
		public Core::String m_Ref;     // the path, symbol, terminal or output source id, or chat id; empty for problems, diff
		public Core::String m_Label;   // as the message writes it, after its @
	}

	// A file sent with a message, kept in the chat's media folder.
	class Attachment
	{
		public Core::String m_Name;
		public Core::String m_Path;    // workspace-relative
		public Core::String m_Mime;
		public int64 m_Bytes;
	}

	// One piece of a turn the clients show: "text", "thinking", "tool", "harness" or "message" (a message sent while the
	// turn ran, where the model got it; "steering" in chats from before 2026-10-23).
	class Segment
	{
		public Core::String m_SegId;
		public Core::String m_Type;
		public Core::String m_Text;          // text, thinking, harness and message segments
		public bool m_Done;
		public Core::String m_Model;
		public Core::String m_Kind;          // a harness note's kind ("continue_after_max_tokens")
		// A tool segment:
		public Core::String m_Tool;
		public Core::String m_CallId;
		public Core::String m_ArgsText;
		public Core::String m_State;         // running, awaiting_approval, ok, error, denied, interrupted
		public Core::String m_Preview;       // the result as the card shows it
		public bool m_IsError;
		public int64 m_Ms;
		public int64 m_Plus;                 // lines a write added and removed
		public int64 m_Minus;
		public Core::String m_Source;        // where the tool ran ("built-in", "Output: AI")
		public Core::Array<IED::ToolImage> m_Images;   // what the result showed the model, for the card
		public int64 m_Tokens;
		// A message segment:
		public Core::Array<Mention> m_Mentions;
		public Core::Array<Attachment> m_Attachments;
		public Core::String m_FromChatId;    // another chat sent it; empty: the owner
		public Core::String m_FromTitle;
		public int64 m_AtMs;                 // when the model was given it

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
		public Core::String m_Role;
		public Core::String m_Content;
		public Core::String m_ToolCalls;     // an assistant's calls, the JSON array of the request format; empty for none
		public Core::String m_ToolCallId;    // a tool result's call
		public Core::Array<IED::ToolImage> m_Images;   // a tool result's images (Images.ch)
		public Core::String m_Reasoning;     // an assistant's thinking in its step: sent back where the server keeps it
		// What compression sends in its place (docs/04 "Context management"), kept apart in the chat's context.json:
		// the content itself never changes. Empty: the content is sent.
		public Core::String m_Shown;
		public bool m_ImagesOut;             // compression left its images out of requests; the chat keeps them

		// The text a request sends: what compression shows, else the content.
		public Core::StringView SentText() const;
		// Whether a request sends its images.
		public bool SendsImages() const;
		// As the chat keeps it: the content whole, whatever compression shows of it.
		public void WriteJson(IED::JsonWriter& json) const;
		// As a request sends it; with `unsent` (why its images are not sent, Images.ch), the images named in its text.
		// With `withReasoning`, its thinking as reasoning_content.
		public void WriteRequest(IED::JsonWriter& json, Core::StringView unsent, bool withReasoning = false) const;
		public void Read(const Serialization::DomNode& node);
	}

	// A message as it waits for a turn - in the queue, or for the running turn's next step: the owner's, or another
	// chat's (ChatTools.ch).
	class OwnerMessage
	{
		public Core::String m_Text;
		public Core::Array<Mention> m_Mentions;
		public Core::Array<Attachment> m_Attachments;
		public Core::String m_FromChatId;    // the chat that sent it; empty: the owner
		public Core::String m_FromTitle;

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
		public Core::String m_TurnId;
		public Core::String m_UserText;
		public Core::Array<Mention> m_Mentions;         // what the message names, read when the turn starts
		public Core::Array<Attachment> m_Attachments;   // what was sent with it
		public Core::String m_FromChatId;               // another chat sent the message (ChatTools.ch); empty: the owner
		public Core::String m_FromTitle;
		public int64 m_Hops;                            // chats in a row that messaged each other up to this turn
		public Core::String m_Resumed;                  // IED started it after a restart (Resume.ch): "relaunch" or "interrupted"
		public Core::String m_Origin;                   // "background": IED started it to tell of background tasks that ended
		public ChatMode m_Mode;
		public Core::String m_Model;
		public Core::String m_Group;
		public Core::String m_Effort;                   // the last request's (docs/04 "Effort"); empty: none was sent
		public Core::Array<Segment*> m_Segments;
		public Core::String m_Stop;                     // empty while it runs
		public Core::Array<Core::String> m_Recoveries;  // what the harness did, in order ("continued after max_tokens")
		public int64 m_TokensIn;                        // the last request's prompt
		public int64 m_TokensOut;                       // every request's completion, summed
		public int64 m_TtftMs;                          // the last request's time to first token
		public int64 m_GenMs;                           // time spent generating after the first token, summed
		public int64 m_StartedMs;
		public int64 m_EndedMs;
		public Core::String m_Prefill;                  // while a request's prompt is read: its server's progress, JSON
		public Core::Array<ModelMessage*> m_Messages;   // what the model is sent of this turn, in order
		public bool m_Compressed;                       // left out of requests to fit the context (kept on disk)

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
		public Core::String m_Path;
		public int64 m_Plus;
		public int64 m_Minus;
	}

	// A tab a chat uses that another plugin opened for it (docs/04 "What a chat has open"): a page the AI drives, ...
	class ChatResource
	{
		public Core::String m_Ref;      // the tab ("browser:pg-3")
		public Core::String m_Title;
		public Core::String m_Kind;     // "page", ...: the side panel's icon
		public Core::String m_Close;    // the UI command that closes it for good (the side panel's Force close); "" for none
	}

	const int64 c_ClipShownBytes = 2048;   // of a clip's text, what the clients get

	// A task a chat's AI started in the background (docs/04 "Background tasks"): a command, a project's task, or a
	// sub-agent's chat. Its fields change under the server lock; its thread is the one that ends it.
	class BackgroundTask
	{
		public Core::String m_TaskId;       // "bg-3"
		public Core::String m_Kind;         // command, task, agent
		public Core::String m_Title;        // "npm test", "build of Game", "agent reviewer"
		public Core::String m_State;        // running, done, failed, stopped, lost
		public int64 m_StartedMs;
		public int64 m_EndedMs;
		public int64 m_ExitCode;
		public Core::String m_Summary;      // how it ended, one line ("exit 0", "build succeeded in 40 s (0 errors)")
		public Core::String m_Output;       // its output's end, or the agent's answer
		public Core::String m_AgentChatId;  // an agent's chat
		public bool m_Told;                 // its chat heard it ended
		public bool m_StopAsked;

		public bool Running() const { return m_State == "running"; }
		// {taskId, kind, title, state, startedMs, endedMs?, exitCode?, summary, chatId?, output?}; the output with `withOutput`.
		public void WriteJson(IED::JsonWriter& json, bool withOutput) const;
		public void Read(const Serialization::DomNode& node);
	}

	// A clip on the AI's clipboard (Clipboard.ch): text it copied, pasted into later calls as {{clip:<name>}}.
	class Clip
	{
		public Core::String m_Name;
		public Core::String m_Text;
		public Core::String m_Source;   // where it came from ("src/ring.cf lines 40-62", "the shell_run result")

		// Its lines; a final line break starts no line.
		public int64 Lines() const;
	}

	class ChatTask
	{
		public Core::String m_Id;
		public Core::String m_Title;
		public Core::String m_Status;        // pending, in_progress, done, skipped
		public Core::String m_Note;
	}

	class Approval
	{
		public Core::String m_ApprovalId;
		public Core::String m_CallId;
		public Core::String m_Tool;
		public Core::String m_Args;          // JSON
		public Core::String m_Summary;       // what the card says it does
		public Core::String m_Why;           // the mode and rule that made it ask
		public Core::String m_Subject;       // the rule "allow for this chat" adds: "fs.write(src/**)"
		public Core::String m_Diff;          // a write's change, unified
		public int64 m_Plus;
		public int64 m_Minus;
		public Core::String m_Decision;      // empty until answered: allow, deny, always, withdrawn
		public Core::String m_Message;       // a denial's reason
		public Core::String m_By;

		public void WriteJson(IED::JsonWriter& json) const;
	}

	class Question
	{
		public Core::String m_QuestionId;
		public Core::String m_CallId;
		public Core::String m_Question;
		public Core::Array<Core::String> m_Options;
		public bool m_AllowText;
		public bool m_Answered;
		public Core::String m_Answer;

		public void WriteJson(IED::JsonWriter& json) const;
	}

	// A turn that runs: what its thread and the chat.* methods share under the server lock. `cancelled` is also read
	// by the step's reads without it.
	class ChatRun
	{
		public Turn* m_Turn;
		public bool m_Cancelled;
		public Threading::ConditionVariable m_Wake;   // an approval or answer came, or the run was stopped
	}

	class Chat
	{
		public Core::String m_ChatId;
		public Core::String m_WsId;
		public Core::String m_Title;
		public ChatMode m_Mode;
		public PermissionMode m_Permission;
		public Core::String m_Group;
		public Core::String m_Effort;                   // low, medium, high, xhigh or max; empty: its role's
		public Core::String m_PinnedModel;              // a model of ai.models over its group's primary; empty: the group's
		public Core::String m_ClaudeSession;            // the Claude Code session holding its conversation (docs/04)
		public Core::Array<Core::String> m_ClaudeTurns; // the turns that session has
		public ChatState m_State;
		public Core::String m_StateReason;              // why it paused
		public int64 m_CreatedMs;
		public int64 m_UpdatedMs;
		public Core::Array<Turn*> m_Turns;
		public Core::Array<ChatTask*> m_Tasks;
		public Core::Array<OwnerMessage*> m_Queued;     // sent while a turn ran, for the next turn
		public Core::Array<OwnerMessage*> m_Pending;    // sent while a turn ran, for its next step
		public Core::Array<Approval*> m_Approvals;       // waiting for a decision
		public Question* m_Question;                    // the ask_user question waiting, or null
		public Core::Array<Core::String> m_Allowed;     // rules the owner allowed for this chat ("Allow for this chat")
		public Core::Array<ModifiedFile*> m_Modified;   // what the AI wrote, with its lines added and removed
		public Core::Array<Core::String> m_LoadedTools; // deferred tools tool_search or a call loaded (docs/13)
		public Core::Array<ChatResource*> m_Resources;  // tabs it uses that plugins reported (chat.resource), this run only
		public Core::Array<Clip*> m_Clips;              // the AI's clipboard, oldest first (Clipboard.ch)
		public Core::Array<BackgroundTask*> m_Background;   // its AI's background tasks, oldest first (Background.ch)
		public Core::String m_Agent;                    // the group's sub-agent this chat runs as; "" for an orchestrator
		public Core::String m_ParentChatId;             // the chat that ran it as a sub-agent; ""
		public Core::String m_Context;                  // the context meter (ContextMeter.ch); empty before a request
		public int64 m_EstimateOffset;                  // the last count a model's server gave less IED's estimate of that request
		public Core::String m_EstimateModel;            // the model that count came from; "" before any (kept in chat.json)
		// What a model server's prefix cache holds of this chat (docs/12 "What makes a server read the whole prompt
		// again"), so a step's read is estimated as what was added rather than the whole prompt: the last request's count
		// (model `estimateModel`), its system message and tools as a hash, and whether a compression changed what
		// requests send since. Kept in chat.json.
		public int64 m_LastPromptTokens;
		public int64 m_PromptHead;
		public bool m_PromptRewritten;
		public bool m_ClaudeAsked;                      // Claude was asked to count its context this server run (ClaudeCount.cf)
		public Core::String m_Resume;                   // a call of this tool restarts the server: the chat goes on once it is back (Resume.ch)
		public ChatRun* m_Run;                          // the running turn, or null
		public int64 m_Counter;

		public Turn* Current() const { return m_Turns.IsEmpty() ? nullptr : m_Turns[m_Turns.Count() - 1]; }
		// It runs on a single model of its own, not a group (docs/04 "One choice per chat"): no group's effort applies.
		public bool RunsAlone() const { return !m_PinnedModel.IsEmpty() && m_Group.IsEmpty() && m_Agent.IsEmpty(); }
		public Core::String NextId(const char* prefix);
		public Core::String Topic() const { return Core::String("chat:") + m_ChatId.View(); }
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
