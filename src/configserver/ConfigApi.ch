// ConfigApi (docs/35 "The API", "The web page"): /config/ on a config server - the page (/config/, its files public:
// they hold no data) and /config/v1/..., where every request carries a token: `Authorization: Bearer <token>`
// (ConfigAccess.ch), or from a browser the page signed in, the cookie `ied-config` with the header `X-IED-Config: 1`
// (another site's page cannot send it without a CORS preflight, which is never granted). None or an unknown token gets
// 401, a reader's write 403. No Host or Origin check (a bearer token is no browser's, and the cookie needs the header),
// and on a server that is not a config server /config/ is 404. Runs on a connection's thread; takes the server lock only
// to read the server plugins' state, and its own for when each token was last used and the sign-in links.

include "ConfigServer.ch"
include "host/Server.ch"

namespace Ied
{
	class ConfigApi
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server) { this.m_Server = server; }

		// Whether the request is the config server's (its path starts /config/): then it is answered.
		public bool Handle(const Http::Request& request, Http::Response& response);

	private:
		private void Route(ConfigServer* config, Core::StringView rest, ConfigRole role, Core::StringView who, bool cookie, const Http::Request& request, Http::Response& response);
		private void Package(ConfigServer* config, Core::StringView rest, ConfigRole role, Core::StringView who, const Http::Request& request, Http::Response& response);
		private void Tokens(ConfigServer* config, Core::StringView name, ConfigRole role, Core::StringView who, bool cookie, const Http::Request& request, Http::Response& response);
		private void Session(ConfigServer* config, ConfigRole role, Core::StringView who, bool cookie, const Http::Request& request, Http::Response& response);
		private void Page(ConfigServer* config, Core::StringView file, const Http::Request& request, Http::Response& response);
		private bool SignInBrowser(ConfigServer* config, const Http::Request& request, Http::Response& response, Core::StringView by, Core::String& name, Core::String& error);
		private Core::String ServerPluginsJson(ConfigServer* config);
		private void Used(Core::StringView who);

		private Server* m_Server;
		private Threading::Mutex m_Lock;
		private Core::HashMap<Core::String, int64> m_LastUsed;   // a token's name: when it was last used (Unix ms)
		private Core::HashMap<Core::String, int64> m_Links;      // a sign-in link's digest: when it dies (Unix ms)
	}

	const char* c_ConfigApiPrefix = "/config/v1/";
	// The cookie a signed-in browser's token is kept in, its header, and how long it lasts and a link lives.
	const char* c_ConfigCookie = "ied-config";
	const char* c_ConfigHeader = "X-IED-Config";
	const int64 c_ConfigCookieMaxAgeS = 31536000;
	const int64 c_ConfigLinkMs = 600000;

	// A browser token's name, "web-<device>-<6 hex>", from a device name ("Edge on Windows": web-edge-on-windows-1a2b3c).
	Core::String WebTokenName(Core::StringView device, Core::StringView suffix);
	// A /config/ page file's name: lowercase letters, digits, '-' and '.', ending .html, .js, .css or .svg.
	bool IsConfigPageFile(Core::StringView file);
}
