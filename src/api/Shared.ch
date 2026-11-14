// A config server's shared settings (docs/35): what a server plugin - C4 code the config server runs from its
// server-plugins/ folder - publishes to every IED that follows it. A published key is a setting of the served settings,
// merged after the hand-written settings.json by the key's merge kind (a hand-written value is the default the plugin's
// refines), and kept in <config root>/published/<plugin id>.json, so it survives restarts and redeploys; removing the
// server plugin removes what it published. Clients get it in their Shared tier, under their own app settings.
//     include "api/Shared.ch"
//     Core::String problem;
//     if (!IED::Shared::Publish(Core::StringView("ai.models"), models.View(), problem)) context.Log(problem.View());

include "bedrock.ch"

namespace IED::Shared
{
	// Publishes `key` = `valueJson` (any JSON value) under the calling plugin's name; any thread of the plugin's. False
	// with `problem` when the server is no config server (ied-server --config-root), the value is not JSON, or the file
	// cannot be written.
	bool Publish(Core::StringView key, Core::StringView valueJson, Core::String& problem);
	// Takes back what the calling plugin published for `key` (nothing to take back is no problem).
	bool Unpublish(Core::StringView key, Core::String& problem);
	// Whether this server is a config server, where Publish works.
	bool IsConfigServer();
}
