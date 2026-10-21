// A plugin's protocol methods (docs/26 section 2b, docs/10): a request a client sends by name, answered by the plugin's
// code on its thread with JSON text, or with an IED::RpcError it throws (a stable error code and a message). A
// third-party plugin's methods are named "<pluginId>/<method>"; a built-in keeps its protocol names (git.*, term.*).

include "protocol.ch"

namespace IED
{
	class Request
	{
		public Core::String method;
		public Params params;
		public Core::String clientId;       // the calling client
		public func<bool()> stopped;        // the client cancelled the request

		public bool Cancelled() const { return stopped(); }
	}

	alias MethodHandler = func<Core::String(const Request& request)>;
}
