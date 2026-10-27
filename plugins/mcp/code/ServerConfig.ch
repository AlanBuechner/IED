// A server's configuration (docs/04 "MCP"): an mcp.servers entry - a command IED starts (stdio) or a URL it reaches
// (streamable HTTP, http or https, with the CA file an https server's certificate may chain to), with what the owner
// says of it: enabled, permission, preload, privacy, the time a call and a start may take, the tools left out - or a
// plugin's contributes.mcpServers entry, which an mcp.servers entry of its name replaces. Names and the AI's tool names
// made from them.

include "api/Plugin.ch"

namespace Mcp
{
	const int64 c_DefaultTimeoutSeconds = 120;
	const int64 c_MostTimeoutSeconds = 1800;
	const int64 c_DefaultStartSeconds = 30;
	const int64 c_MostStartSeconds = 300;
	const int64 c_ToolNameMostBytes = 64;

	enum class Permission { Ask, Auto, Read }

	// A value of `env` or `headers`: text, or the name of a variable of IED's environment.
	class ConfigValue
	{
		public Core::String m_Name;
		public Core::String m_Text;
		public Core::String m_FromEnv;
	}

	class ServerConfig
	{
		public Core::String m_Name;
		public Core::String m_Source;              // "settings", or "plugin <id>"
		public Core::String m_Command;
		public Core::Array<Core::String> m_Args;
		public Core::Array<ConfigValue> m_Env;
		public Core::String m_Cwd;
		public Core::String m_Url;
		public Core::Array<ConfigValue> m_Headers;
		public Core::String m_CaFile;               // https: CA certificates trusted besides the system's, as written
		public bool m_Enabled;
		public Permission m_Permission;
		public bool m_PreloadAll;
		public Core::Array<Core::String> m_Preload;
		public Core::String m_Privacy;              // local, lan, cloud
		public int64 m_TimeoutSeconds;
		public int64 m_StartSeconds;
		public Core::Array<Core::String> m_Exclude;
		public Core::String m_Hash;                 // of what a start uses and what its tools look like to the AI

		public bool IsHttp() const { return !m_Url.IsEmpty(); }
		public bool Preloads(Core::StringView tool) const;
		public bool Excludes(Core::StringView tool) const;
	}

	// The servers of an mcp.servers value (JSON), in its order; an entry that cannot be used is left out with a problem
	// ("mcp.servers.<name>: ...").
	Core::Array<ServerConfig*> ReadServers(Core::StringView json, Core::Array<Core::String>& problems);
	// One entry; null with `problem`.
	ServerConfig* ReadServer(Core::StringView name, const Serialization::DomNode& entry, Core::String& problem);
	// A plugin's contributes.mcpServers entry, ${pluginFolder} replaced by `folder`; null with `problem`.
	ServerConfig* ReadContributedServer(const Serialization::DomNode& entry, Core::StringView pluginId, Core::StringView folder, Core::String& problem);
	// Whether `name` may name a server: 1 to 32 letters, digits, '_' and '-'.
	bool ValidServerName(Core::StringView name);
	// The AI's name for `server`'s tool `tool`: "mcp_<server>_<tool>", lowercase, anything but letters, digits and '_' as
	// '_', runs of '_' as one, at most kToolNameMostBytes.
	Core::String ToolName(Core::StringView server, Core::StringView tool);
	// The group of a server's tools: "mcp_<server>".
	Core::String ToolGroup(Core::StringView server);
	// The command line a server runs as shown to the owner, or its URL.
	Core::String Display(const ServerConfig* config);
	// The privacy a server has when its entry says none: local for a command or a loopback URL, cloud otherwise.
	Core::String DefaultPrivacy(Core::StringView url);
	const char* PermissionName(Permission permission);
}
