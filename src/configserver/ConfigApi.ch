// ConfigApi (docs/35 "The API"): /config/v1/... on a config server. Every request carries `Authorization: Bearer
// <token>` (ConfigAccess.ch): none or an unknown one gets 401, a reader's write 403. It is no browser's: no session, no
// Host or Origin check (a page cannot send the header to another origin), and on a server that is not a config server
// /config/ is 404. Runs on a connection's thread; takes the server lock only to read the server plugins' state.

include "ConfigServer.ch"

namespace Ied
{
	class ConfigApi
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server) { this.m_Server = server; }

		// Whether the request is the config API's (its path starts /config/): then it is answered.
		public bool Handle(const Http::Request& request, Http::Response& response);

	private:
		private void Route(ConfigServer* config, Core::StringView rest, ConfigRole role, Core::StringView who, const Http::Request& request, Http::Response& response);
		private void Package(ConfigServer* config, Core::StringView rest, ConfigRole role, Core::StringView who, const Http::Request& request, Http::Response& response);
		private Core::String ServerPluginsJson(ConfigServer* config);

		private Server* m_Server;
	}

	const char* c_ConfigApiPrefix = "/config/v1/";
}
