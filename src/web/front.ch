// The HTTP side (docs/10, docs/11): GET /ws becomes a protocol session; /healthz; uploads and blobs; the web client's
// files, plugin files, /ui-kit/, /codicons/ and /vendor/; /api/auth/*. Every request's Host must be a loopback name or the bind
// address (DNS rebinding), unless the server listens on every interface; /ws, uploads and auth POSTs also check Origin.
// Everything but /healthz, /api/auth/* and the login page's files needs a session (AuthGate).

include "session.ch"
include "authgate.ch"

namespace Ied
{
	class Front
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server) { this.server = server; gate = AuthGate(server); }

		// On a connection's thread.
		public void Handle(const Http::Request& request, Http::Response& response);

	private:
		private bool HostAllowed(const Http::Request& request) const;
		private bool OriginAllowed(const Http::Request& request) const;
		private void Route(Core::StringView path, const Http::Request& request, Http::Response& response, Device* device);
		private void Unauthenticated(Core::StringView path, const Http::Request& request, Http::Response& response);
		private void WebSocket(const Http::Request& request, Http::Response& response, Device* device);
		private void Health(Http::Response& response);
		private void Upload(const Http::Request& request, Http::Response& response);
		private void Blob(Core::StringView id, const Http::Request& request, Http::Response& response);
		private void StaticFile(Core::StringView path, Http::Response& response);
		private void FolderFile(const FS::Path& root, Core::StringView path, Http::Response& response);
		private void PluginFile(Core::StringView wsId, Core::StringView path, Http::Response& response);
		// "/w/<wsId>/files/<path>": a workspace's image for a preview (docs/10); 404 for any other type.
		private void WorkspaceImage(Core::StringView wsId, Core::StringView path, Http::Response& response);
		private void Codicon(Core::StringView file, Http::Response& response);
		private void VendorFile(Core::StringView path, Http::Response& response);
		private void SendFile(const FS::Path& file, Http::Response& response);

		private Server* server;
		private AuthGate gate;
	}

	// A path's percent escapes decoded; false for a malformed one.
	bool PercentDecode(Core::StringView text, Core::String& decoded);
	// The Content-Type of a file by its extension, in any case.
	const char* ContentTypeOf(Core::StringView extension);
	// "<name>.svg" with a name of lowercase letters, digits and '-': a codicon file name.
	bool IsCodiconFile(Core::StringView file);
}
