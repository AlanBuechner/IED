// Diagnostics (docs/10 "Topics", diag:<wsId>): every file's problems by source - a language server's
// ("<profile>/<project>"), a build's ("<matcher source>/<project>", docs/06) - as UTF-16 JSON arrays, published as they
// change. Members run with the server lock held.

include "workspaces.ch"

namespace Ied
{
	class FileDiagnostics
	{
		public Core::HashMap<Core::String, Core::String> bySource;
	}

	class Diagnostics
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		public void WorkspaceOpened(Workspace* ws);
		// `items` (a JSON array, "[]" for none) are what `source` says of `path` now.
		public void Set(Workspace* ws, Core::StringView path, Core::StringView source, Core::StringView items);
		// Every source's diagnostics of `path`, gone (its file is not there any more).
		public void Forget(Workspace* ws, Core::StringView path);
		// Every path's diagnostics from `source`, gone (a new build of what it checked).
		public void ClearSource(Workspace* ws, Core::StringView source);

	private:
		private Core::String Snapshot(Workspace* ws) const;

		private Server* server;
		private Core::HashMap<Core::String, FileDiagnostics*> files;   // "<wsId>/<path>"
	}

	Core::String DiagnosticsTopic(const Workspace* ws);
}
