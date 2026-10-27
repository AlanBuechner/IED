// The server's settings from its command line.
//     ied-server [--host 127.0.0.1] [--port 8765] [--home <folder>] [--static frontend] [--data <folder>]
//                [--app-data <folder>] [--control <name>] [--workspace <folder>]... [--test-hooks]
//                [--idle-timeout <s>] [--outbound-budget-mb <n>] [--coalesce-kb <n>] [--allow-origin <o>]...
//                [--allow-host <h>]... [--quiet] [--debug-scripts on|off] [--relaunched]
//     ied-server --role workspace --ws-id <wsId> --workspace <path> --master <pipe> --control <pipe> --data <folder>
//                --app-data <folder> --home <folder> --state <folder> --log <file> [--restarted-after <text>] ...
// The first is the master (docs/01 "Processes"); the second a workspace server, which only the master starts, with
// its secret in IED_SERVER_SECRET (docs/11 "Workspace servers").

include "bedrock.ch"

namespace Ied
{
	// Which server this process is (docs/01 "Processes").
	enum class ServerRole { Master, Workspace }

	class ServerConfig
	{
		public Core::String version;
		public ServerRole role;                        // --role: master (the default) or workspace
		public Core::String wsId;                      // a workspace server's workspace (--ws-id)
		public Core::String masterControl;             // a workspace server's master's pipe (--master)
		public Core::String stateDir;                  // the server's own state (--state); empty: the data folder
		public Core::String logPath;                   // its log (--log); empty: <data>/logs/server.log
		public Core::String restartedAfter;            // how the run before this one ended, when the master restarted it
		public Core::String secret;                    // a workspace server's: what every request the master sends carries
		public Core::String host;
		public int32 port;
		public bool portGiven;                         // --port was given: no other port will do
		public Core::String homeDir;                   // the install: frontend/, plugins/, shared/; empty: HomeFolder's search
		public Core::String staticDir;                 // the web client's files; empty: <home>/frontend
		public Core::String appDataDir;                // app-tier settings and plugins (docs/07); empty: AppDataFolder's default
		public Core::String dataDir;                   // the server's own state (docs/11); empty: DataFolder's default
		public Core::String controlName;               // the control channel's pipe (docs/10); empty: ControlName's default
		public Core::Array<Core::String> workspaces;   // folders to open
		public Core::Array<Core::String> allowOrigins;
		public Core::Array<Core::String> allowHosts;
		public bool testHooks;                         // testhooks.* (tools/conformance), never in normal use
		public bool quiet;
		public bool relaunched;                        // started by a server that relaunched (relaunch.ch): it waits for the pipe and the port
		public Core::Array<Core::String> arguments;    // as given, for a relaunch
		// Plugin code as debug scripts, DLLs with PDBs that are never kept in the script cache (C4/docs/runtime/04
		// section 6.4): 1 on, 0 off, -1 as the server was built (on for a -g build).
		public int32 debugScripts = -1;
		public int64 idleTimeoutMs;
		public int64 outboundBudget;
		public int64 coalesceBytes;
	}

	ServerConfig DefaultConfig();
	bool IsWorkspaceServer(const ServerConfig& config);
	// --state, or the data folder.
	Core::String StateFolder(const ServerConfig& config);
	// --log, or <data>/logs/server.log.
	Core::String LogPath(const ServerConfig& config);
	// --data, or %LOCALAPPDATA%\IED; with --test-hooks a fresh temporary folder.
	Core::String DataFolder(const ServerConfig& config);
	// --home, or the nearest folder above this program that holds shared/host-api/schemas; empty when none does.
	Core::String HomeFolder(const ServerConfig& config);
	// --static, or <home>/frontend.
	Core::String StaticFolder(const ServerConfig& config);
	// --app-data, or %APPDATA%\IED; with --test-hooks <data>/app.
	Core::String AppDataFolder(const ServerConfig& config);
	// --control, or "ied-<user>" (the single-instance lock); with --test-hooks one of its own.
	Core::String ControlName(const ServerConfig& config);
	// The host a browser opens: the bind address, or loopback when the server listens on every interface.
	Core::String BrowserHost(const ServerConfig& config);
	// "http://<host>:<port>/", with "?t=<token>" and "#<fragment>" when they are not empty.
	Core::String LaunchUrl(const ServerConfig& config, int32 port, Core::StringView token, Core::StringView fragment);
	// `config` from the arguments after the program's name; false with `error` saying which one is wrong.
	bool ParseArguments(const Core::Array<Core::String>& arguments, ServerConfig& config, Core::String& error);
}
