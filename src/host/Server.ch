// Server: the state every connection and request shares - the session epoch, the topic hub, the methods, the sessions
// and the services - under one lock. A method runs holding it (web/Rpc.ch); a session takes it for each frame.

include "Config.ch"
include "LogFile.ch"
include "Relaunch.ch"
include "Servers.ch"
include "MasterLink.ch"
include "web/Session.ch"
include "web/Proxy.ch"
include "services/Presence.ch"
include "services/Workspaces.ch"
include "services/Documents.ch"
include "services/TestHooks.ch"
include "services/Blobs.ch"
include "services/Outputs.ch"
include "services/Layouts.ch"
include "services/Trash.ch"
include "services/Search.ch"
include "services/Diagnostics.ch"
include "auth/Auth.ch"
include "plugins/Host.ch"
include "plugins/CodeCalls.ch"
include "Trust.ch"
include "Recent.ch"
include "Schemas.ch"
include "Appdata.ch"
include "settings/Service.ch"
include "lsp/Pool.ch"
include "tasks/Service.ch"
include "dap/Service.ch"
include "tools/Service.ch"
include "projects/Templates.ch"
include "projects/Manage.ch"

namespace Ied
{
	class Server
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(ServerConfig config);

		// A line of the server's log: to standard error (unless --quiet), to logs/server.log and to every workspace's
		// "ied" output source. Safe from any thread, holding the server lock or not.
		public void Log(Core::StringView line);
		// The lock is held.
		public void RemoveSession(Session* session);
		// A simulated restart (testhooks.restart), on a thread of its own holding no lock: a new epoch, rings and opIds
		// forgotten, durable state kept, and every session told bye and closed.
		public void Restart();
		// Which server this is (docs/01 "Processes").
		public bool IsMaster() const { return m_Config.m_Role == ServerRole::Master; }
		// The lock is held: a trust choice kept - by the master in trust.json and the audit log, by a workspace server in
		// memory and through the master; false when it could not be saved.
		public bool RecordTrust(const FS::Path& root, bool trusted);

		private void LogStart();

		public Launch::LogFile m_LogFile;
		public LogPump m_LogPump;
		public Threading::Thread m_LogThread;
		public Threading::Mutex m_Lock;
		public Threading::ConditionVariable m_Changed;   // an opId's first run finished
		public ServerConfig m_Config;
		public int32 m_Port;   // the port it listens on, once it does
		public Core::String m_Epoch;
		public Hub m_Hub;
		public Rpc m_Rpc;
		public Core::Array<Session*> m_Sessions;
		public Presence* m_Presence;
		public Workspaces* m_Workspaces;
		public Documents* m_Documents;
		public TestHooks* m_TestHooks;
		public Blobs* m_Blobs;
		public Outputs* m_Outputs;
		public Layouts* m_Layouts;
		public Trash* m_Trash;
		public Search* m_Search;
		public Diagnostics* m_Diagnostics;
		public LspPool* m_Lsp;
		public Tasks* m_Tasks;
		public Breakpoints* m_Breakpoints;
		public Debugger* m_Debugger;
		public Tools* m_Tools;
		public ProjectTemplates* m_Projects;
		public ProjectManager* m_ProjectManager;
		public Auth* m_Auth;
		public PluginHost* m_Plugins;
		public CodeRuntime* m_Code;
		public TrustStore m_Trust;
		public RecentProjects m_Recent;
		public HostSchemas m_Schemas;
		public Settings* m_Settings;
		public AppDataWatch m_AppWatch;
		public Relaunch m_Relaunch;
		public WorkspaceServers* m_Servers;   // the master's
		public Proxy* m_Proxy;                // the master's
		public MasterLink* m_Master;          // a workspace server's; null in the master
		public int64 m_StartedMs;
	}

	// "e-" and 12 random hex digits.
	Core::String NewEpoch();

	// The log's lines between Log and the thread that publishes them under the server lock.
	class LogPump
	{
		public void Push(Core::StringView line);
		// Waits for lines, then hands them to `server`'s outputs with its lock held; forever.
		public void Run(Server* server);

		private Threading::Mutex m_Lock;
		private Threading::ConditionVariable m_Ready;
		private Core::Array<Core::String> m_Pending;
	}

	// "[hh:mm:ss] " in local time.
	Core::String LogStamp(int64 unixNs);
}
