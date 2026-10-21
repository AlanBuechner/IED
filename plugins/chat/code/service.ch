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

namespace Ai
{
	const int64 kChatSnapshotTurns = 20;
	const char* kNewChatTitle = "New chat";

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
		// A turn for `text` starts.
		public void StartTurn(Chat* chat, Core::StringView text);
		// A turn ended: the next queued message, if the turn finished, starts the next.
		public void TurnEnded(Chat* chat);
		public Core::String ModelOf(const Chat* chat) const;

		public IED::PluginContext* context;
		public Prompts prompts;
		public ToolRegistry* tools;
		public Core::HashMap<Core::String, double> prefillRates;   // model -> prompt tokens per second, measured
		public Core::HashMap<Core::String, int64> windows;         // model -> context size its server reported or an error named
		public Core::HashMap<Core::String, bool> probed;           // models whose server was asked (docs/12 discovery)

	private:
		private Core::String Snapshot(Chat* chat);
		private Core::String ListSnapshot(Core::StringView wsId);
		private void AddTopic(Chat* chat);
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
		private Core::String DeleteMethod(const IED::Request& request);
		private Core::String ProbeMethod(const IED::Request& request);

		private Core::Array<Chat*> chats;
	}

	Core::String ChatsTopic(Core::StringView wsId);
	// The workspace `wsId` the plugin is loaded for; throws not_found.
	IED::Workspace OpenWorkspace(Core::StringView wsId);
}
