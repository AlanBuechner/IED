// Server: the state every connection and request shares - the session epoch, the topic hub, the methods, the sessions
// and the services - under one lock. A method runs holding it (web/rpc.ch); a session takes it for each frame.

include "config.ch"
include "web/session.ch"
include "services/presence.ch"
include "services/workspaces.ch"
include "services/documents.ch"
include "services/testhooks.ch"
include "services/blobs.ch"
include "services/outputs.ch"
include "services/layouts.ch"
include "services/trash.ch"
include "services/search.ch"
include "services/diagnostics.ch"
include "services/terminals.ch"
include "auth/auth.ch"
include "plugins/host.ch"
include "trust.ch"
include "schemas.ch"
include "appdata.ch"
include "settings/service.ch"
include "lsp/pool.ch"
include "tasks/service.ch"
include "dap/service.ch"
include "ai/service.ch"
include "git/service.ch"

namespace Ied
{
	class Server
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(ServerConfig config);

		// A line of the server's log: to standard error (unless --quiet) and to every workspace's "ied" output source.
		// Safe from any thread, holding the server lock or not.
		public void Log(Core::StringView line);
		// The lock is held.
		public void RemoveSession(Session* session);
		// A simulated restart (testhooks.restart), on a thread of its own holding no lock: a new epoch, rings and opIds
		// forgotten, durable state kept, and every session told bye and closed.
		public void Restart();

		public LogPump logPump;
		public Threading::Thread logThread;
		public Threading::Mutex lock;
		public Threading::ConditionVariable changed;   // an opId's first run finished
		public ServerConfig config;
		public int32 port;   // the port it listens on, once it does
		public Core::String epoch;
		public Hub hub;
		public Rpc rpc;
		public Core::Array<Session*> sessions;
		public Presence* presence;
		public Workspaces* workspaces;
		public Documents* documents;
		public TestHooks* testHooks;
		public Blobs* blobs;
		public Outputs* outputs;
		public Layouts* layouts;
		public Trash* trash;
		public Search* search;
		public Diagnostics* diagnostics;
		public LspPool* lsp;
		public Tasks* tasks;
		public Terminals* terminals;
		public Breakpoints* breakpoints;
		public Debugger* debugger;
		public Chats* chats;
		public Git* git;
		public Auth* auth;
		public PluginHost* plugins;
		public TrustStore trust;
		public HostSchemas schemas;
		public Settings* settings;
		public AppDataWatch appWatch;
	}

	// "e-" and 12 random hex digits.
	Core::String NewEpoch();

	// The log's lines between Log and the thread that publishes them under the server lock.
	class LogPump
	{
		public void Push(Core::StringView line);
		// Waits for lines, then hands them to `server`'s outputs with its lock held; forever.
		public void Run(Server* server);

		private Threading::Mutex lock;
		private Threading::ConditionVariable ready;
		private Core::Array<Core::String> pending;
	}

	// "[hh:mm:ss] " in local time.
	Core::String LogStamp(int64 unixNs);
}
