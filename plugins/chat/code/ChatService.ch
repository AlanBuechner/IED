// Chats (docs/10 "Chats", docs/04, docs/12), the ied.chat plugin's server code: every workspace's chats, loaded from
// disk when it opens; the chats:<wsId> and chat:<chatId> topics; the chat.* methods; and the runtime state turns share -
// the prompts, the tools, each model's measured prefill rate and a context size an error revealed. Members run inside
// IED::HostLock (the methods take it, as the server's own did), so the topics' snapshots read the chats whole.

include "api/Plugin.ch"
include "chats/ChatStore.ch"
include "prompts/Prompts.ch"
include "chats/Models.ch"
include "aitools/Catalog.ch"
include "run/Probe.ch"
include "claude/ClaudeAccount.ch"
include "claude/ClaudeSession.ch"
include "run/Resume.ch"
include "context/ContextMeter.ch"

namespace Ai
{
	const int64 c_ChatSnapshotTurns = 20;
	const char* c_NewChatTitle = "New chat";
	// What a call a restart interrupted gets as its result (ChatStore.ch LoadChats).
	const char* c_InterruptedNote = "This call was interrupted by a server restart; its effect is unknown. Check the current state before retrying it.";

	// What resuming a chat's Claude session to count its context needs (ClaudeCount.cf).
	class ClaudeCountStart
	{
		public ModelProfile* m_Profile;
		public Core::String m_System;
		public Core::String m_Tools;
		public int64 m_ToolsBytes;   // of the tool definitions as a request lists them
		public Core::String m_Effort;
		public Core::Array<Core::String> m_Command;
	}

	class Chats
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context);

		public void WorkspaceOpened(const IED::Workspace& ws);
		public Chat* Get(Core::StringView chatId) const;
		// Whether `chatId` is a deleted chat, kept until it is purged (DeletedChats.cf).
		public bool IsDeleted(Core::StringView chatId) const;
		// The workspace `chat` belongs to; throws not_found when it is not open.
		public IED::Workspace WorkspaceOf(const Chat* chat) const;

		// For runs:
		public void Publish(Chat* chat, const char* kind, Core::StringView data);
		public void PublishMergeable(Chat* chat, const char* kind, const IED::Merge& merge);
		// The chat's summary changed: chat.changed on its topic and its workspace's, and its header saved.
		public void Changed(Chat* chat);
		// A turn for `message` starts - the owner's, or another chat's (docs/04 "Chats talk to each other"); `resumed` why IED
		// starts it itself after a restart (Resume.ch), "" for a message.
		public void StartTurn(Chat* chat, const OwnerMessage& message);
		// `origin` "background": IED starts it to tell of background tasks that ended.
		public void StartTurn(Chat* chat, const OwnerMessage& message, int64 hops, const char* resumed = "", const char* origin = "");
		// The workspace's chats.
		public Core::Array<Chat*> InWorkspace(Core::StringView wsId) const;
		// A new chat in `ws`, published on its chats: topic.
		public Chat* Create(const IED::Workspace& ws, Core::StringView title, ChatMode mode, Core::StringView group);
		// Another chat's message: true when it started a turn of `chat`, false when its running turn gets it at the next
		// step. The model is told which chat sent it.
		public bool Deliver(Chat* chat, Core::StringView text, const Chat* from, int64 hops);
		// A message for the running turn's next step (docs/12 "Messages while a turn runs"), told to the clients and kept.
		public void Pend(Chat* chat, OwnerMessage* message);
		// queue.changed: the queued and the pending messages.
		public void QueueChanged(Chat* chat);
		// A turn ended: the next queued message, if the turn finished, starts the next.
		public void TurnEnded(Chat* chat);
		public Core::String ModelOf(const Chat* chat) const;
		// The group the chat runs in: its own, else ai.defaultGroup, else the first group.
		public Core::String GroupOfChat(const Chat* chat, const IED::Workspace& ws) const;
		// A sub-agent chat's turn ended: its parent's task for it ends with its answer (Agents.cf).
		public void AgentTurnEnded(Chat* chat);
		// What the chat's next turn starts on: the model its chain's pace order puts first (its first model when Claude is
		// used up, "" for none), with its group and that role's effort. Inside the host lock.
		public Core::String NextModel(const Chat* chat, const IED::Workspace& ws, Core::String& group, Core::String& roleEffort) const;
		// The chat's Claude session (docs/04 "A chat's session"), made from what its header kept the first time this
		// server run asks.
		public ClaudeSession* SessionOf(Chat* chat);
		// The context window of `profile`: its own contextWindow, or what its server or the CLI reported when that is
		// smaller or the profile names none; 0 when nothing said.
		public int64 WindowOf(const ModelProfile* profile) const;
		// A window a server, the CLI or an error reported for `model`, kept in the plugin's state folder so the next
		// server run knows it before asking; whether it changed.
		public bool LearnWindow(Core::StringView model, int64 window);
		// Claude's own count of an idle Claude chat's context asked of its resumed session, on a thread of its own; once
		// per server run (ClaudeCount.cf). Inside the host lock.
		public void CountClaude(Chat* chat, const IED::Workspace& ws);

		// Background tasks (Background.cf, docs/04 "Background tasks"); inside the host lock. A new running task of
		// `chat`, published.
		public BackgroundTask* StartBackground(Chat* chat, Core::StringView kind, Core::StringView title);
		// `task` ended, its fields set: its chat is told - a running turn at its next step, an idle chat by a turn of its own.
		public void BackgroundEnded(Chat* chat, BackgroundTask* task);
		// The tasks that ended and were not told, now told: the note that tells the model; "" for none.
		public Core::String TellEnded(Chat* chat);
		public bool HasUntold(const Chat* chat) const;
		// background.changed with the chat's tasks, and chat.changed (its summary counts the running ones).
		public void BackgroundChanged(Chat* chat);
		// Asks a running task to stop: a command is killed, a project's task cancelled, an agent's turn stopped.
		public void StopBackground(BackgroundTask* task);

		public IED::PluginContext* m_Context;
		public ClaudeAccount* m_Claude;                              // the plan's usage (docs/04 "Usage")
		public Prompts m_Prompts;
		public ToolRegistry* m_Tools;
		public Core::HashMap<Core::String, double> m_PrefillRates;   // model -> prompt tokens per second, measured
		public Core::HashMap<Core::String, int64> m_Windows;         // model -> context size its server reported or an error named
		public Core::HashMap<Core::String, bool> m_Probed;           // models whose server answered the ask (docs/12 discovery)
		public Core::HashMap<Core::String, int64> m_ProbeFailedMs;   // models whose server did not, and when
		public Core::HashMap<Core::String, bool> m_LlamaCpp;         // models llama.cpp serves: they stream prompt progress and read effort in the template
		public Core::HashMap<Core::String, bool> m_SeesImages;       // models whose server takes images (llama.cpp's modalities)

	private:
		private Core::String Snapshot(Chat* chat);
		private void LoadWindows();
		private void WakeForBackground(Chat* chat);
		private void AskClaudeCount(Chat* chat, IED::Workspace ws);
		private ClaudeSession* ClaudeCountSetup(Chat* chat, const IED::Workspace& ws, ClaudeCountStart& start);
		// The context meter of an idle chat redrawn for the window of `model`, the one its next turn starts on.
		private void MeterFor(Chat* chat, const IED::Workspace& ws, Core::StringView model);
		private Core::String ListSnapshot(Core::StringView wsId);
		private void AddTopic(Chat* chat);
		private Chat* NewChat(const IED::Workspace& ws, Core::StringView title, ChatMode mode, Core::StringView group);
		private void Announce(const IED::Workspace& ws, Chat* chat);
		// The method `name`, its body inside IED::HostLock.
		private void Register(const char* name, IED::MethodHandler method);

		private Core::String ListMethod(const IED::Request& request);
		private Core::String CreateMethod(const IED::Request& request);
		private Core::String SetMethod(const IED::Request& request);
		private Core::String SendMethod(const IED::Request& request);
		private Core::String UnqueueMethod(const IED::Request& request);
		private Core::String StopMethod(const IED::Request& request);
		private Core::String StopTaskMethod(const IED::Request& request);
		private Core::String ApproveMethod(const IED::Request& request);
		private Core::String AnswerMethod(const IED::Request& request);
		private Core::String HistoryMethod(const IED::Request& request);
		private Core::String ProbeMethod(const IED::Request& request);

		// Reverting a chat to before one of its turns (Revert.cf, docs/04 "Reverting a chat").
		private Core::String RevertMethod(const IED::Request& request);
		// The chat's running turn stopped and waited for; throws conflict when it does not end in time.
		private void StopForRevert(Chat* chat, const IED::Request& request);
		// A Claude Code session that holds one of the `dropped` turns is stopped and forgotten: the next turn starts a new one.
		private void LeaveDroppedClaude(Chat* chat, const Core::Array<Core::String>& dropped);

		// Deleted chats (docs/04 "Deleting a chat", DeletedChats.cf): chat.delete moves a chat among them, chat.restore
		// brings it back, chat.purge removes it for good, and so does the keeper ai.chats.keepDeletedDays after.
		private Core::String DeleteMethod(const IED::Request& request);
		private Core::String RestoreMethod(const IED::Request& request);
		private Core::String PurgeMethod(const IED::Request& request);
		private void LoadDeleted(const IED::Workspace& ws);
		private void WriteDeleted(IED::JsonWriter& json, Core::StringView wsId) const;
		private DeletedChat* DeletedOf(Core::StringView chatId) const;
		private void Purge(DeletedChat* chat);
		// The deleted chats kept past ai.chats.keepDeletedDays, purged; the lock is held.
		private void PurgeExpired();
		// The thread that purges expired deleted chats every kPurgeEveryMs.
		private void DeletedKeeper();
		// The thread that asks for Claude's usage every ai.claude.usageRefreshMinutes and stops idle sessions.
		private void ClaudeKeeper();
		// Stops the Claude sessions no turn used for kClaudeIdleMs; the lock is held.
		private void StopIdleSessions();
		// The workspace's chats that go on by themselves after a restart (Resume.ch), kResumeDelayNs after it opened.
		private void ResumeLater(Core::StringView wsId);
		private void Resume(Core::StringView wsId);

		private Core::Array<Chat*> m_Chats;
		private Core::Array<DeletedChat*> m_Deleted;
		private Core::HashMap<Core::String, ClaudeSession*> m_ClaudeSessions;   // by chat
		private bool m_Stopping;
	}

	Core::String ChatsTopic(Core::StringView wsId);
	// What the model is told of `message` sent to `to`: its words; another chat's says which chat sent it, and a sub-agent's
	// task from the chat that ran it says its answer goes back by itself.
	Core::String ModelText(const Chat* to, const OwnerMessage& message);
	// ai.probe of a claude-code profile: the CLI asked now (where it is, its version, whether it is signed in), answered in
	// a probe's shape.
	Core::String ProbeClaude(ClaudeAccount* account, IED::PluginContext* context, const ModelProfile& profile);

	// The master's account keeper's state: stopping once the plugin stops.
	class AccountKeeper
	{
		public bool m_Stopping;
	}

	// The master's account kept current (docs/04 "Usage"): the CLI asked every ai.claude.usageRefreshMinutes while
	// Claude is set up, and asked for a sign-in while it is not; on a thread of the plugin's, until `keeper` stops.
	void KeepAccount(IED::PluginContext* context, ClaudeAccount* account, AccountKeeper* keeper);
	// The workspace `wsId` the plugin is loaded for; throws not_found.
	IED::Workspace OpenWorkspace(Core::StringView wsId);
}
