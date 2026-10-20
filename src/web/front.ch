// The HTTP side (docs/10, docs/11): GET /ws becomes a protocol session; /healthz; uploads and blobs; the web client's
// files. Every request's Host must be a loopback name or the bind address (DNS rebinding), unless the server listens on
// every interface; /ws and uploads also check Origin.

include "session.ch"

namespace Ied
{
	class Front
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server) { this.server = server; }

		// On a connection's thread.
		public void Handle(const Http::Request& request, Http::Response& response);

	private:
		private bool HostAllowed(const Http::Request& request) const;
		private bool OriginAllowed(const Http::Request& request) const;
		private void WebSocket(const Http::Request& request, Http::Response& response);
		private void Health(Http::Response& response);
		private void Upload(const Http::Request& request, Http::Response& response);
		private void Blob(Core::StringView id, const Http::Request& request, Http::Response& response);
		private void StaticFile(Core::StringView path, Http::Response& response);

		private Server* server;
	}

	// A path's percent escapes decoded; false for a malformed one.
	bool PercentDecode(Core::StringView text, Core::String& decoded);
	const char* ContentTypeOf(Core::StringView extension);
}
