// LspPool (docs/24): the language servers of every workspace - started when a document one serves opens, fed the
// document store's changes, asked by clients through lsp.request with positions and URIs rewritten both ways - and
// what they publish: diagnostics on "diag:<wsId>", their states on "lsp:<wsId>". Every member runs with the server
// lock held.

include "client.ch"
include "rewrite.ch"
include "services/documents.ch"

namespace Ied
{
	// One file's diagnostics from each source (a profile id), as UTF-16 JSON arrays.
	class FileDiagnostics
	{
		public Core::HashMap<Core::String, Core::String> bySource;
	}

	class LspPool
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// The services' events.
		public void WorkspaceOpened(Workspace* ws);
		public void TrustChanged(Workspace* ws);
		public void DocumentOpened(Document* doc);
		public void DocumentChanged(Document* doc);
		public void DocumentSaved(Document* doc);
		// `doc` was at `oldPath`: closed there on the servers that had it, opened where it is now.
		public void DocumentRenamed(Document* doc, Core::StringView oldPath);
		// IED stops: shutdown and exit to every server.
		public void StopAll();

		// What a LanguageServer hands over.
		public void Running(LanguageServer* language);
		public void StateChanged(LanguageServer* language);
		public void Log(LanguageServer* language, Core::StringView text);
		public void Notification(LanguageServer* language, Core::StringView method, const Serialization::DomNode& params);
		// The result's JSON for a request the server made.
		public Core::String ServerRequest(LanguageServer* language, Core::StringView method, const Serialization::DomNode& params);
		// Its profile's initializationOptions with its initializationOptionsFile merged over them (the file's keys win).
		public Core::String InitializationOptions(const LanguageServer* language);

	private:
		// The profiles of the plugins loaded for `ws` whose `when` holds, in load order.
		private Core::Array<LspProfile*> ProfilesFor(Workspace* ws);
		private bool Allowed(Workspace* ws, const LspProfile* profile);
		private LanguageServer* Find(Workspace* ws, Core::StringView profileId, Core::StringView project) const;
		private LanguageServer* Create(Workspace* ws, LspProfile* profile, const ProjectModel* project);
		// The server that serves `doc` - created and started, with `start`, when there is none yet - or null.
		private LanguageServer* Serving(Document* doc, bool start);
		private LanguageServer* Ready(Call& call, Document* doc);
		private void OpenOn(LanguageServer* language, Document* doc);
		private void Diagnostics(LanguageServer* language, const Serialization::DomNode& params);
		private void SetDiagnostics(Workspace* ws, Core::StringView path, Core::StringView source, Core::StringView items);
		private void ClearDiagnostics(LanguageServer* language);
		// Every source's diagnostics of a path, gone (its file is not there any more).
		private void ForgetDiagnostics(Workspace* ws, Core::StringView path);
		// lsp.refresh: clients fetch `kinds` again for `path` (empty: every document).
		private void Refresh(Workspace* ws, Core::StringView path, const Core::Array<Core::String>& kinds);
		private Core::String DiagnosticsSnapshot(Workspace* ws) const;
		private Core::String StatusSnapshot(Workspace* ws) const;
		private bool TextOf(Workspace* ws, Core::StringView uri, Core::String& text);
		private const LineIndex* LinesOf(Workspace* ws, Core::StringView uri);
		private Core::String ToClientUri(Workspace* ws, Core::StringView uri) const;
		private Core::String ToServerUri(Core::StringView uri) const;
		// The rewrite of a message to (or from) `language`, whose positions without a URI belong to `uri`.
		private Rewrite RewriteFor(LanguageServer* language, Core::StringView uri, bool toServer);
		private Core::String RequestMethod(Call& call, const Params& params);
		private Core::String CapabilitiesMethod(Call& call, const Params& params);
		private Core::String StatusMethod(Call& call, const Params& params);
		private Core::String RestartMethod(Call& call, const Params& params);

		private Server* server;
		private Core::Array<LanguageServer*> languages;
		private Core::HashMap<Core::String, FileDiagnostics*> diagnostics;   // "<wsId>/<path>"
		private Core::HashMap<Core::String, LineIndex*> lineCache;           // by URI, for one message
	}

	// A params object's JSON with "textDocument": {"uri": `uri`} in place of any it had.
	Core::String WithTextDocument(Core::StringView params, Core::StringView uri);
	// Code actions cut to those whose diagnostics meet `range` (or that have none), for servers that return them all.
	Core::String FilterCodeActions(const Serialization::DomNode& actions, const Serialization::DomNode& range);
	Core::String DiagnosticsTopic(const Workspace* ws);
	Core::String LspTopic(const Workspace* ws);
}
