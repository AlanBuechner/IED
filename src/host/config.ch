// The server's settings from its command line.
//     ied-server [--host 127.0.0.1] [--port 8765] [--static frontend] [--workspace <folder>]... [--test-hooks]
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
		public Core::String staticDir;                 // the web client's files; empty: none
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
	// `config` from the arguments after the program's name; false with `error` saying which one is wrong.
	bool ParseArguments(const Core::Array<Core::String>& arguments, ServerConfig& config, Core::String& error);
}
