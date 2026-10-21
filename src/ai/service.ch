// Chats (docs/10 "Chats", docs/04, docs/12): every workspace's chats, loaded from disk when it opens; the chats:<wsId>
// and chat:<chatId> topics; the chat.* methods; and the runtime state turns share - the prompts, the tools, each
// model's measured prefill rate and a context size an error revealed. Members run with the server lock held.

include "chatstore.ch"
include "prompts.ch"
include "models.ch"
include "tools/catalog.ch"
include "probe.ch"
include "web/rpc.ch"

namespace Ied
{
	const int64 kChatSnapshotTurns = 20;
	const char* kNewChatTitle = "New chat";

	class Chats
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		public void WorkspaceOpened(Workspace* ws);
		public Chat* Get(Core::StringView chatId) const;
		public Workspace* WorkspaceOf(const Chat* chat) const;

		// For runs:
		public void Publish(Chat* chat, const char* kind, Core::StringView data);
		public void PublishMergeable(Chat* chat, const char* kind, const Merge& merge);
		// The chat's summary changed: chat.changed on its topic and its workspace's, and its header saved.
		public void Changed(Chat* chat);
		// A turn for `text` starts.
		public void StartTurn(Chat* chat, Core::StringView text);
		// A turn ended: the next queued message, if the turn finished, starts the next.
		public void TurnEnded(Chat* chat);
		public Core::String ModelOf(const Chat* chat) const;

		public Prompts prompts;
		public ToolRegistry* tools;
		public Core::HashMap<Core::String, double> prefillRates;   // model -> prompt tokens per second, measured
		public Core::HashMap<Core::String, int64> windows;         // model -> context size its server reported or an error named
		public Core::HashMap<Core::String, bool> probed;           // models whose server was asked (docs/12 discovery)

	private:
		private Core::String Snapshot(Chat* chat);
		private Core::String ListSnapshot(Workspace* ws);
		private void AddTopic(Chat* chat);

		private Core::String ListMethod(Call& call, const Params& params);
		private Core::String CreateMethod(Call& call, const Params& params);
		private Core::String SetMethod(Call& call, const Params& params);
		private Core::String SendMethod(Call& call, const Params& params);
		private Core::String UnqueueMethod(Call& call, const Params& params);
		private Core::String StopMethod(Call& call, const Params& params);
		private Core::String ApproveMethod(Call& call, const Params& params);
		private Core::String AnswerMethod(Call& call, const Params& params);
		private Core::String HistoryMethod(Call& call, const Params& params);
		private Core::String DeleteMethod(Call& call, const Params& params);
		private Core::String ProbeMethod(Call& call, const Params& params);

		private Server* server;
		private Core::Array<Chat*> chats;
	}

	Core::String ChatsTopic(const Workspace* ws);
}
