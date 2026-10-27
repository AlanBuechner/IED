// Debug adapters (docs/25 "Adapters"): what a plugin's `dapProviders` contribution says. The last loaded plugin's
// adapter of a type wins, as language server profiles do; its command, arguments, environment and folder take the
// variables of docs/24 "Profiles and selection".

include "lsp/Profiles.ch"

namespace Ied
{
	class DapProvider
	{
		public Core::String m_Type;
		public Core::String m_Label;
		public Core::String m_PluginId;
		public FS::Path m_PluginDir;
		public Core::String m_Command;
		public Core::Array<Core::String> m_Args;
		public Core::Array<Core::String> m_Environment;   // "NAME=value"
		public Core::String m_Cwd;
		public Core::Array<Core::String> m_Languages;
		public bool m_Staged;
		public bool m_Tcp;                                // transport tcp: the adapter connects to IED's port, ${port} in its args
		public Core::String m_When;
	}

	// The adapters of one plugin's `contributes`, in order.
	// One dapProviders entry.
	DapProvider* ReadDapProvider(const Serialization::DomNode& entry, Core::StringView pluginId, const FS::Path& pluginDir);
	void ReadDapProviders(const Serialization::DomNode& contributes, Core::StringView pluginId, const FS::Path& pluginDir, Core::Array<DapProvider*>& providers);
	// The last of `providers` of `type`, or null.
	DapProvider* ProviderOfType(const Core::Array<DapProvider*>& providers, Core::StringView type);
}
