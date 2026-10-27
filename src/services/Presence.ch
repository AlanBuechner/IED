// The "server" topic (docs/10, F-SRV-24): the open workspaces, the connected clients and the notifications; and
// server.info, server.notify, server.dismiss.

include "web/Rpc.ch"

namespace Ied
{
	class Notification
	{
		public Core::String m_Id;
		public Core::String m_Level;
		public Core::String m_Text;
		public int64 m_Ts;
	}

	class Presence
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// The lock is held.
		public void Connected(Session* session);
		public void Disconnected(Session* session);
		// The master: its plugins' code registered a method or a topic; routes.changed when the routes did.
		public void RoutesChanged();
		// {methods, topics}: what a client sends the master (docs/10 "Routing").
		public Core::String RoutesJson() const;
		// A notification for every device (server.notify's): its id. A workspace server's goes to the master, whose
		// `server` topic every tab follows (master.notify; its id is the master's, so "" here). The lock is held.
		public Core::String Add(Core::StringView level, Core::StringView text);

	private:
		private Core::String Snapshot() const;
		private Core::String Info(Call& call, const Params& params);
		private Core::String Notify(Call& call, const Params& params);
		private Core::String Dismiss(Call& call, const Params& params);

		private Server* m_Server;
		private Core::Array<Notification> m_Notifications;
		private int64 m_NextNote;
		private Core::String m_Published;   // the routes told last
	}

	Core::String NotificationJson(const Notification& note);
}
