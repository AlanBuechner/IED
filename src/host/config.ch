// The server's settings from its command line.
//     ied-server [--host 127.0.0.1] [--port 8765] [--static frontend] [--data <folder>] [--control <name>]
//                [--workspace <folder>]... [--test-hooks]
//                [--idle-timeout <s>] [--outbound-budget-mb <n>] [--coalesce-kb <n>] [--allow-origin <o>]...
//                [--allow-host <h>]... [--quiet]

include "bedrock.ch"

namespace Ied
{
	class ServerConfig
	{
		public Core::String version;
		public Core::String host;
		public int32 port;
		public bool portGiven;                         // --port was given: no other port will do
		public Core::String staticDir;                 // the web client's files; empty: none
		public Core::String dataDir;                   // the server's own state (docs/11); empty: DataFolder's default
		public Core::String controlName;               // the control channel's pipe (docs/10); empty: ControlName's default
		public Core::Array<Core::String> workspaces;   // folders to open
		public Core::Array<Core::String> allowOrigins;
		public Core::Array<Core::String> allowHosts;
		public bool testHooks;                         // testhooks.* (tools/conformance), never in normal use
		public bool quiet;
		public int64 idleTimeoutMs;
		public int64 outboundBudget;
		public int64 coalesceBytes;
	}

	ServerConfig DefaultConfig();
	// --data, or %LOCALAPPDATA%\IED; with --test-hooks a fresh temporary folder.
	Core::String DataFolder(const ServerConfig& config);
	// --control, or "ied-<user>" (the single-instance lock); with --test-hooks one of its own.
	Core::String ControlName(const ServerConfig& config);
	// The host a browser opens: the bind address, or loopback when the server listens on every interface.
	Core::String BrowserHost(const ServerConfig& config);
	// "http://<host>:<port>/", with "?t=<token>" and "#<fragment>" when they are not empty.
	Core::String LaunchUrl(const ServerConfig& config, int32 port, Core::StringView token, Core::StringView fragment);
	// `config` from the arguments after the program's name; false with `error` saying which one is wrong.
	bool ParseArguments(const Core::Array<Core::String>& arguments, ServerConfig& config, Core::String& error);
}
