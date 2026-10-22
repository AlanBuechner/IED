// What SearXNG is started with (docs/29 "SearXNG"): the settings that shape it and the settings.yml they make - text
// only, so the plugin restarts SearXNG exactly when that text would change and tests it without a SearXNG.

include "bedrock.ch"

namespace WebSearch
{
	struct SearxConfig
	{
		public Core::Array<Core::String> engines;   // as EngineNames gives them
		public int32 safeSearch;                     // 0 off, 1 moderate, 2 strict
		public int32 timeoutSeconds;                 // each engine's request
		public Core::String proxy;                   // "socks5h://user:password@127.0.0.1:port"; "" for none
	}

	// SearXNG's defaults, then: only the configured engines loaded and each turned on, JSON answers, no limiter, short
	// rests for engines that refused, and - with a proxy - every request through it. SearXNG's own Tor check
	// (using_tor_proxy) stays off: the proxy is the Tor plugin's, and the check's request through a fresh circuit at start
	// failed SearXNG's start in testing. The secret key is not in it (the launcher makes one per start).
	Core::String SettingsYaml(const SearxConfig& config);
	// websearch.safeSearch's "off", "moderate", "strict" as SearXNG's 0, 1, 2 (0 for anything else).
	int32 SafeSearchLevel(Core::StringView name);
	// A websearch.engines value's names: lowercased, trimmed, without empties and repeats.
	Core::Array<Core::String> EngineNames(const Serialization::DomNode& list);
	// `text` in ASCII lowercase.
	Core::String Lowercase(Core::StringView text);
}
