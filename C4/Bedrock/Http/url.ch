// URLs a client reaches (D-66): http://, https://, ws:// and wss://, split into what a connection and a request need.
//     Http::Url url = Http::ParseUrl(Core::StringView("https://[::1]:8443/mcp?x=1"));   // secure, "::1", 8443, "/mcp?x=1"

include "Text/string.ch"
include "Text/stringview.ch"

namespace Http
{
	// The ports a URL without one means.
	const int32 kDefaultHttpPort = 80;
	const int32 kDefaultHttpsPort = 443;

	// The parts of "http://host[:port][/path][?query]", or of an https://, ws:// or wss:// URL; `valid` is false for
	// anything else (another scheme, no host, a bad port). An IPv6 host is written in brackets: "http://[::1]:8080/".
	struct Url
	{
		public bool valid;
		public bool webSocket;        // ws:// or wss:// rather than http:// or https://
		public bool secure;           // https:// or wss://: over TLS
		public Core::String host;     // without brackets
		public int32 port;            // when the URL gives none: 80, or 443 when secure
		public Core::String target;   // "/path?query"; "/" when the URL has no path
	}

	Url ParseUrl(Core::StringView text);
	// The Host header for `url`: its host, in brackets when IPv6, and its port unless it is its scheme's own.
	Core::String HostHeader(const Url& url);
}
