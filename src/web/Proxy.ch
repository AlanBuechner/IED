// Proxy (the master, docs/10 "Transport", docs/11 "Workspace servers"): every /w/<wsId>/... request passed to the
// server of workspace `wsId` once the master has checked it - HTTP fetched and answered as the workspace server answered
// it, the WebSocket joined to one of the workspace server's own, message for message both ways. The workspace server is
// waited for while it starts or restarts (kServerAwaitMs); a tunnel ends when either side closes, and the browser's side
// is told how the workspace server's ended (1012 when it went away without a word), so the client reconnects.

include "AuthGate.ch"
include "host/Servers.ch"

namespace Ied
{
	// A workspace server's socket, kept on the heap for the threads that pump it.
	class Upstream
	{
		public Http::WebSocket m_Socket;
	}

	// One proxied WebSocket.
	class Tunnel
	{
		public Core::String m_WsId;
		public Core::String m_DeviceId;
		public Http::WebSocket* m_Client;
		public Upstream* m_Upstream;
		// Held while another thread uses `client`, which lives on the browser connection's thread: that thread takes it
		// before it returns.
		public Threading::Mutex m_Use;
		public bool m_Gone;
	}

	class Proxy
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server) { this.m_Server = server; }

		// On a connection's thread, `device` the session the master checked: `/w/<wsId>/<rest>` to wsId's server.
		public void Forward(const Http::Request& request, Http::Response& response, Core::StringView wsId, Device* device);
		// Every tunnel of `deviceId` told bye auth-revoked and closed (1008); any thread.
		public void Revoke(Core::StringView deviceId);
		// How many tunnels are open: the clients of workspace servers the master passes.
		public int64 Count();

	private:
		private void WebSocket(const Http::Request& request, Http::Response& response, Core::StringView wsId, int32 port, Core::StringView secret, Device* device);
		private void Fetch(const Http::Request& request, Http::Response& response, Core::StringView target, int32 port, Core::StringView secret, Device* device);
		private void Pump(Tunnel* tunnel);
		private void Add(Tunnel* tunnel);
		private void Remove(Tunnel* tunnel);

		private Server* m_Server;
		private Threading::Mutex m_Lock;
		private Core::Array<Tunnel*> m_Tunnels;
	}

	// The workspace a /w/<wsId>/... path names and what follows it ("/ws", "/files/a.png"); false for another path.
	bool WorkspaceRoute(Core::StringView path, Core::StringView& wsId, Core::StringView& rest);
	// The target a workspace server is asked for: /ws and /api/... without the /w/<wsId> prefix, the rest as it is.
	Core::String UpstreamTarget(Core::StringView path, Core::StringView rest, Core::StringView query);
	// The master's workspace that holds `wsId`: itself, or the workspace a plugin-opened "<wsId>.<n>" belongs to.
	Core::StringView ServingWorkspace(Core::StringView wsId);
	// The close code a browser gets when its workspace server's socket closed with `code`.
	int32 TunnelCloseCode(int32 code);
}
