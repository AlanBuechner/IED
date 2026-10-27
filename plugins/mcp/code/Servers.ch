// The MCP servers of each workspace (docs/04 "MCP" Lifetime): their configurations (mcp.servers, the plugins'
// mcpServers), each one's state - stopped, starting, running, failed - its connection, and its tools, known from its
// last start or from .ied/local/mcp/tools.json for the same configuration; the AI's tools made of them (the plugin's
// computed toolCalls for the workspace), the topic mcp:<wsId> and the MCP output. A watcher thread stops servers no call
// used for mcp.idleMinutes and starts again those that exited. Every field is under `lock`, never held while a start, a
// call or the plugin API runs; `publishing` keeps the workspace's contributions, cache and topic in the order they changed.

include "Client.ch"
include "ServerConfig.ch"
include "Content.ch"

namespace Mcp
{
	const int64 c_WatchTickMs = 250;
	const int64 c_QuickExitMs = 60000;          // an exit this soon after its start counts toward giving up
	const int64 c_MostQuickExits = 5;
	const int64 c_MostRestartDelayMs = 30000;
	const int64 c_EnsureMostWaitMs = 30000;
	const int64 c_ListPagesMost = 50;
	const int64 c_LogLinesKept = 40;
	const int64 c_InstructionsMostBytes = 2400;

	enum class ServerState { Stopped, Starting, Running, Failed }

	const char* ServerStateName(ServerState state);

	class McpTool
	{
		public Core::String m_Name;          // the server's
		public Core::String m_AiName;        // the chat's: mcp_<server>_<tool>
		public Core::String m_Title;
		public Core::String m_Description;
		public Core::String m_Schema;        // its inputSchema as JSON
		public bool m_ReadOnlyHint;
		public Core::String m_Note;          // why its schema was not taken
	}

	// A resource a server lists (resources/list): an @ mention in the composer (docs/04 "MCP" Resources).
	class McpResource
	{
		public Core::String m_Uri;
		public Core::String m_Name;
		public Core::String m_Title;
		public Core::String m_Description;
		public Core::String m_MimeType;
	}

	// The lines a start printed, for the error it fails with and Settings' Test.
	class StartLog
	{
		public void Add(Core::StringView line);
		// The last `count` lines, joined with " | ".
		public Core::String Tail(int64 count);

		private Threading::Mutex m_Mutex;
		private Core::Array<Core::String> m_Lines;
	}

	const int64 c_AnyGeneration = -1;     // a lookup of an entry by name alone
	const int64 c_TestGeneration = -2;    // a test's connection, which no entry has

	class ServerEntry
	{
		public ServerConfig* m_Config;
		public ServerState m_State;
		public McpClient* m_Client;
		public Core::Array<McpTool*> m_Tools;
		public bool m_ToolsKnown;
		public Core::String m_ToolsHash;     // the configuration they were listed with
		public Core::Array<McpResource*> m_Resources;
		public Core::String m_ServerName;
		public Core::String m_ServerVersion;
		public Core::String m_ProtocolVersion;
		public Core::String m_Instructions;
		public Core::String m_Error;
		public Core::Array<Core::String> m_Log;   // its last lines of standard error and IED's notes
		public int64 m_Restarts;
		public int64 m_QuickExits;
		public int64 m_RestartAtMs;          // a restart after an exit is due (0: none)
		public int64 m_StartedMs;
		public int64 m_LastUsedMs;
		public int64 m_StartByMs;            // a start under way gives up then
		public int64 m_Generation;           // bumped by each start and stop: an old connection's end is not the new one's
		public int64 m_Calls;                // calls running now
	}

	class WorkspaceServers
	{
		public IED::Workspace m_Ws;
		public Core::Array<ServerEntry*> m_Servers;
		public Core::Array<Core::String> m_Problems;   // entries of mcp.servers that cannot be used
		public int64 m_IdleMs;                          // mcp.idleMinutes; 0 never
		public Serialization::DomNode m_Cached;         // tools.json as read when the workspace opened
		public Core::String m_Contributed;              // the toolCalls given last (under `publishing`)
		public Core::String m_Section;                  // the system-message sections given last
		public Core::String m_Cache;                    // tools.json as written last
	}

	// A call the mcp.call binding started: its own thread runs it, mcp.result takes what it came to.
	class RunningCall
	{
		public Core::String m_CallId;
		public bool m_Done;
		public bool m_Cancelled;
		public IED::ToolResult m_Result;
	}

	class McpService
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context) { this.m_Context = context; }

		// The watcher thread.
		public void Start();
		// Every server stopped (the plugin stops).
		public void Shutdown();
		// OnWorkspace and OnTrustChanged: the workspace as it is now, its servers read again.
		public void Opened(const IED::Workspace& ws);
		// settings.changed: every workspace's servers read again; a changed one stops and its tools are listed again at
		// its next start.
		public void SettingsChanged(Core::StringView wsId);
		// mcp.ensure: the starts a turn waits for.
		public Core::String Ensure(Core::StringView wsId);
		public Core::String StatusJson(Core::StringView wsId);
		// mcp.test: the server `name` started (again, when it runs), or - with `entryJson` - a configuration not saved yet,
		// tried apart and stopped: {ok, ms, serverName?, serverVersion?, protocolVersion?, instructions?, tools: [{name,
		// description}], error?, log}.
		public Core::String Test(Core::StringView wsId, Core::StringView name, Core::StringView entryJson);
		public Core::String Restart(Core::StringView wsId, Core::StringView name);
		public Core::String StopServer(Core::StringView wsId, Core::StringView name);
		// mcp.tool: {server, tool, title, permission, state} of the chat's tool `aiName`, or empty with `error` when it
		// cannot be called now.
		public Core::String DescribeTool(Core::StringView wsId, Core::StringView aiName, Core::String& error);
		// mcp.resources: every resource the workspace's servers list, {resources: [{server, uri, name, title?,
		// description?, mimeType?}]}; servers whose lists are not known yet are started (mcp.ensure).
		public Core::String ResourcesJson(Core::StringView wsId);
		// mcp.readResource: the resource's contents as the model reads them (text; a blob saved under
		// .ied/local/mcp/media and named), or empty with `error`.
		public Core::String ReadResource(Core::StringView wsId, Core::StringView server, Core::StringView uri, Core::String& error);
		// mcp.call, mcp.result, mcp.cancel.
		public Core::String StartCall(const Serialization::DomNode& args, Core::String& error);
		public Core::String CallResult(Core::StringView callId, int64 waitMs, Core::String& error);
		public void CancelCall(Core::StringView callId);

		// What a connection's threads hand on: a line it printed, a notification, its end (`generation` its start's).
		public void ServerLog(Core::StringView wsId, Core::StringView name, int64 generation, Core::StringView line);
		public void Notified(Core::StringView wsId, Core::StringView name, int64 generation, Message* message);
		public void Ended(Core::StringView wsId, Core::StringView name, int64 generation, int64 exitCode);
		// Threads of the plugin's own.
		public void Launch(WorkspaceServers* place, ServerEntry* entry, int64 generation);
		public void Relist(Core::StringView wsId, Core::StringView name, int64 generation);
		public void RelistResources(Core::StringView wsId, Core::StringView name, int64 generation);
		public void RunCall(RunningCall* call, Core::StringView wsId, Core::StringView aiName, Core::StringView arguments);
		public void Watch();

		private WorkspaceServers* Find(Core::StringView wsId);
		private ServerEntry* FindEntryLocked(WorkspaceServers* place, Core::StringView name, int64 generation);
		private ServerEntry* FindEntry(WorkspaceServers* place, Core::StringView name, int64 generation);
		private Core::String TailLocked(ServerEntry* entry, int64 lines);
		private void Reread(WorkspaceServers* place);
		private Core::Array<ServerConfig*> Configs(WorkspaceServers* place, Core::Array<Core::String>& problems);
		private bool Begin(WorkspaceServers* place, ServerEntry* entry, int64& generation);
		private bool Connect(WorkspaceServers* place, ServerConfig* config, int64 generation, McpClient*& client, Core::Array<McpTool*>& tools, Core::Array<McpResource*>& resources, StartLog* printed, Core::String& error);
		private void Halt(WorkspaceServers* place, ServerEntry* entry, Core::StringView why, bool keepFailed);
		private bool AwaitRunning(WorkspaceServers* place, ServerEntry* entry, const bool* cancelled, Core::String& error);
		private IED::ToolResult Invoke(WorkspaceServers* place, ServerEntry* entry, McpTool* tool, Core::StringView arguments, const bool* cancelled);
		private Core::String SaveMedia(const IED::Workspace& ws, Core::StringView server, const ContentPart* part);
		private void Changed(WorkspaceServers* place);
		private void Note(WorkspaceServers* place, ServerEntry* entry, Core::StringView line, Core::StringView name);
		private void Output(Core::StringView wsId, Core::StringView line);
		private Core::String SnapshotJson(WorkspaceServers* place);
		private Core::String ContributionsJson(WorkspaceServers* place);
		private Core::String SectionJson(WorkspaceServers* place);
		private void ReadCacheFile(WorkspaceServers* place);
		private void LoadCached(WorkspaceServers* place, ServerEntry* entry);
		private Core::String CacheJson(WorkspaceServers* place);

		private IED::PluginContext* m_Context;
		private Threading::Mutex m_Lock;
		private Threading::ConditionVariable m_Changed;
		private Threading::Mutex m_Publishing;
		private Core::Array<WorkspaceServers*> m_Places;
		private Core::Array<RunningCall*> m_Running;
		private int64 m_CallCount;
		private int64 m_MediaCount;
		private bool m_Stopping;
	}

	// The tools of a tools/list page with the chat's names for them (mcp_<server>_<tool>, not yet made unique), and its
	// nextCursor.
	Core::Array<McpTool*> ReadToolsPage(Core::StringView server, const Serialization::DomNode& result, Core::String& cursor);
	// The resources of a resources/list page, and its nextCursor.
	Core::Array<McpResource*> ReadResourcesPage(const Serialization::DomNode& result, Core::String& cursor);
	// The delay before the restart after `exits` exits in a row: 1, 2, 4, 8, 16 s, then 30 s.
	int64 RestartDelayMs(int64 exits);
	// A tool's inputSchema as the chat's validator can load it and a model takes it: the server's less the keywords Bedrock's
	// validator does not know (unevaluatedProperties, $dynamicRef, ...; named in `note`), or {"type":"object"} with `note`.
	Core::String UsableSchema(const Serialization::DomNode& schema, Core::String& note);
}
