// Debug adapters (docs/25 "Adapters"): what a plugin's `dapProviders` contribution says. The last loaded plugin's
// adapter of a type wins, as language server profiles do; its command, arguments, environment and folder take the
// variables of docs/24 "Profiles and selection".

include "lsp/profiles.ch"

namespace Ied
{
	class DapProvider
	{
		public Core::String type;
		public Core::String label;
		public Core::String pluginId;
		public FS::Path pluginDir;
		public Core::String command;
		public Core::Array<Core::String> args;
		public Core::Array<Core::String> environment;   // "NAME=value"
		public Core::String cwd;
		public Core::Array<Core::String> languages;
		public bool staged;
		public bool tcp;                                // transport tcp: the adapter connects to IED's port, ${port} in its args
		public Core::String when;
	}

	// The adapters of one plugin's `contributes`, in order.
	// One dapProviders entry.
	DapProvider* ReadDapProvider(const Serialization::DomNode& entry, Core::StringView pluginId, const FS::Path& pluginDir);
	void ReadDapProviders(const Serialization::DomNode& contributes, Core::StringView pluginId, const FS::Path& pluginDir, Core::Array<DapProvider*>& providers);
	// The last of `providers` of `type`, or null.
	DapProvider* ProviderOfType(const Core::Array<DapProvider*>& providers, Core::StringView type);
}
