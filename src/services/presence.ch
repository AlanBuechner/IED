// The "server" topic (docs/10, F-SRV-24): the open workspaces, the connected clients and the notifications; and
// server.info, server.notify, server.dismiss.

include "web/rpc.ch"

namespace Ied
{
	class Notification
	{
		public Core::String id;
		public Core::String level;
		public Core::String text;
		public int64 ts;
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

		private Server* server;
		private Core::Array<Notification> notifications;
		private int64 nextNote;
		private Core::String published;   // the routes told last
	}

	Core::String NotificationJson(const Notification& note);
}
