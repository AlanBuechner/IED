// Chats (docs/10 "Chats", docs/04, docs/12), the ied.chat plugin's server code: every workspace's chats, loaded from
// disk when it opens; the chats:<wsId> and chat:<chatId> topics; the chat.* methods; and the runtime state turns share -
// the prompts, the tools, each model's measured prefill rate and a context size an error revealed. Members run inside
// IED::HostLock (the methods take it, as the server's own did), so the topics' snapshots read the chats whole.

include "api/plugin.ch"
include "chatstore.ch"
include "prompts.ch"
include "models.ch"
include "catalog.ch"
include "probe.ch"
include "claudeaccount.ch"
include "claudesession.ch"

namespace Ai
{
	const int64 kChatSnapshotTurns = 20;
	const char* kNewChatTitle = "New chat";
	// What a call a restart interrupted gets as its result (chatstore.ch LoadChats).
	const char* kInterruptedNote = "This call was interrupted by a server restart; its effect is unknown. Check the current state before retrying it.";

	class Chats
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context);

		public void WorkspaceOpened(const IED::Workspace& ws);
		public Chat* Get(Core::StringView chatId) const;
		// The workspace `chat` belongs to; throws not_found when it is not open.
		public IED::Workspace WorkspaceOf(const Chat* chat) const;

		// For runs:
		public void Publish(Chat* chat, const char* kind, Core::StringView data);
		public void PublishMergeable(Chat* chat, const char* kind, const IED::Merge& merge);
		// The chat's summary changed: chat.changed on its topic and its workspace's, and its header saved.
		public void Changed(Chat* chat);
		// A turn for `message` starts; `from` the chat that sent it (docs/04 "Chats talk to each other"), null for the owner.
		public void StartTurn(Chat* chat, const OwnerMessage& message);
		public void StartTurn(Chat* chat, const OwnerMessage& message, const Chat* from, int64 hops);
		// The workspace's chats.
		public Core::Array<Chat*> InWorkspace(Core::StringView wsId) const;
		// A new chat in `ws`, published on its chats: topic.
		public Chat* Create(const IED::Workspace& ws, Core::StringView title, ChatMode mode, Core::StringView group);
		// Another chat's message: true when it started a turn of `chat`, false when its running turn gets it at the next
		// step. The model is told which chat sent it.
		public bool Deliver(Chat* chat, Core::StringView text, const Chat* from, int64 hops);
		// A turn ended: the next queued message, if the turn finished, starts the next.
		public void TurnEnded(Chat* chat);
		public Core::String ModelOf(const Chat* chat) const;
		// What the chat's next turn starts on: the model its chain's pace order puts first (its first model when Claude is
		// used up, "" for none), with its group and that role's effort. Inside the host lock.
		public Core::String NextModel(const Chat* chat, const IED::Workspace& ws, Core::String& group, Core::String& roleEffort) const;
		// The chat's Claude session (docs/04 "A chat's session"), made from what its header kept the first time this
		// server run asks.
		public ClaudeSession* SessionOf(Chat* chat);

		public IED::PluginContext* context;
		public ClaudeAccount* claude;                              // the plan's usage (docs/04 "Usage")
		public Prompts prompts;
		public ToolRegistry* tools;
		public Core::HashMap<Core::String, double> prefillRates;   // model -> prompt tokens per second, measured
		public Core::HashMap<Core::String, int64> windows;         // model -> context size its server reported or an error named
		public Core::HashMap<Core::String, bool> probed;           // models whose server was asked (docs/12 discovery)
		public Core::HashMap<Core::String, bool> llamaCpp;         // models llama.cpp serves: they stream prompt progress and read effort in the template
		public Core::HashMap<Core::String, bool> seesImages;       // models whose server takes images (llama.cpp's modalities)

	private:
		private Core::String Snapshot(Chat* chat);
		private void QueueChanged(Chat* chat);
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
		private Core::String ApproveMethod(const IED::Request& request);
		private Core::String AnswerMethod(const IED::Request& request);
		private Core::String HistoryMethod(const IED::Request& request);
		private Core::String ProbeMethod(const IED::Request& request);

		// Deleted chats (docs/04 "Deleting a chat", deletedchats.cf): chat.delete moves a chat among them, chat.restore
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

		private Core::Array<Chat*> chats;
		private Core::Array<DeletedChat*> deleted;
		private Core::HashMap<Core::String, ClaudeSession*> claudeSessions;   // by chat
		private bool stopping;
	}

	Core::String ChatsTopic(Core::StringView wsId);
	// ai.probe of a claude-code profile: the CLI asked now (where it is, its version, whether it is signed in), answered in
	// a probe's shape.
	Core::String ProbeClaude(ClaudeAccount* account, IED::PluginContext* context, const ModelProfile& profile);
	// The workspace `wsId` the plugin is loaded for; throws not_found.
	IED::Workspace OpenWorkspace(Core::StringView wsId);
}
