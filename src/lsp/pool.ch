// LspPool (docs/24): the language servers of every workspace - started when a document one serves opens, fed the
// document store's changes, asked by clients through lsp.request with positions and URIs rewritten both ways - and
// what they publish: diagnostics (services/diagnostics), their states on "lsp:<wsId>". Every member runs with the server
// lock held.

include "client.ch"
include "rewrite.ch"
include "services/documents.ch"

namespace Ied
{
	const int64 kLspTransientMost = 16;      // files open on one server for plugin code alone
	const int64 kLspIndexWaitMs = 30000;     // how long workspace symbols wait for the servers' indexes
	const int64 kLspIndexGraceMs = 2000;     // a server this new is given this long to begin indexing
	const int64 kLspIndexSliceNs = 50000000;

	// The server that named a file outside its workspace in a result passed to a client.
	class NamedFile
	{
		public Core::String profile;
		public Core::String project;
	}

	class LspPool
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// The services' events.
		public void WorkspaceOpened(Workspace* ws);
		public void TrustChanged(Workspace* ws);
		// Its solution was read again: a project-scoped server whose project went, or moved to another folder, stops (and
		// one started before the workspace had projects); the open documents go to the servers that serve them now.
		public void ProjectsChanged(Workspace* ws);
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
		// Its profile's initializationOptions with its initializationOptionsFile merged over them (the file's keys win), and
		// with a pluginCodeOption the workspace's plugin code scopes under that key (docs/26 "Editing plugin code").
		public Core::String InitializationOptions(const LanguageServer* language);
		// Whether one of the workspace's servers named the file outside it (`uri` as CanonicalFileUri gives it) in a
		// result passed to a client: only such a file opens as a document (docs/10 "Files outside the workspace").
		public bool WasNamed(Workspace* ws, Core::StringView uri) const;
		// Records that the server (`profile`, `project`) named the file outside `ws`; the first to name it serves it. The
		// test hooks name one with no server.
		public void NameFile(Workspace* ws, Core::StringView uri, Core::StringView profile, Core::StringView project);

		// For plugin code (src/api/lsp.cf, docs/24 "Plugin code asks language servers"). `path` is a workspace path, or
		// the file: URI of a file outside the workspace a server named.
		// `method` to the server serving `path`, the file opened on it when no document holds it; `withDocument` sets the
		// params' textDocument. The result as a client gets it; throws as lsp.request does.
		public Core::String RequestFor(Call& call, Workspace* ws, Core::StringView path, Core::StringView method, Core::StringView params, bool withDocument);
		// {profile, capabilities, aiRequests} of the server serving `path`, started; {profile: null} when none does.
		public Core::String ServingFor(Call& call, Workspace* ws, Core::StringView path);
		// Whether the servers `ws`'s files call for were started (StartFor) in this server run.
		public bool Scanned(Workspace* ws) const;
		// Starts the servers `paths` (the workspace's files) call for, and waits for the workspace's servers to run.
		public void StartFor(Call& call, Workspace* ws, const Core::Array<Core::String>& paths);
		// workspace/symbol of every running server of `ws`: a JSON array, each symbol with its `source`.
		public Core::String SymbolsFor(Call& call, Workspace* ws, Core::StringView query);
		// The text of a file outside `ws` one of its servers named; false for another.
		public bool NamedText(Workspace* ws, Core::StringView uri, Core::String& text);

	private:
		// Its profile's initializationOptions with its initializationOptionsFile merged over them.
		private Core::String ProfileOptions(const LanguageServer* language);
		// The profiles of the plugins loaded for `ws` whose `when` holds, in load order.
		private Core::Array<LspProfile*> ProfilesFor(Workspace* ws);
		private bool Allowed(Workspace* ws, const LspProfile* profile);
		private LanguageServer* Find(Workspace* ws, Core::StringView profileId, Core::StringView project) const;
		private LanguageServer* Create(Workspace* ws, LspProfile* profile, const ProjectModel* project);
		// The server that serves `doc` - created and started, with `start`, when there is none yet - or null.
		private LanguageServer* Serving(Document* doc, bool start);
		private LanguageServer* ServingPath(Workspace* ws, Core::StringView path, bool start);
		// The server that named a file outside `ws` (its file: URI), if it is still there.
		private LanguageServer* NamerOf(Workspace* ws, Core::StringView uri) const;
		// The server running, or throws unavailable.
		private void AwaitServer(Call& call, LanguageServer* language);
		private LanguageServer* Ready(Call& call, Document* doc);
		// The server serving plugin code's `path`, running, with the file open on it; throws not_found or unavailable.
		private LanguageServer* ReadyFor(Call& call, Workspace* ws, Core::StringView path);
		private void OpenOn(LanguageServer* language, Document* doc);
		// `path`, held by no document, opened on `language` with its text on disk, or its copy there brought up to date;
		// past kLspTransientMost the oldest is closed.
		private void OpenTransient(LanguageServer* language, Workspace* ws, Core::StringView path);
		// `method` with `params` (a client's: UTF-16, ied: URIs) to `language` about the file `clientUri` / `serverUri`:
		// the result for clients.
		private Core::String Asked(Call& call, LanguageServer* language, Core::StringView clientUri, Core::StringView serverUri, Core::StringView method, Core::StringView params);
		// Waits, the lock released, while one of `servers` reports work in progress (an index being built), and while one
		// started less than kLspIndexGraceMs ago has not begun any; at most `ms`.
		private void AwaitIndexes(Call& call, const Core::Array<LanguageServer*>& servers, int64 ms);
		private void Diagnostics(LanguageServer* language, const Serialization::DomNode& params);
		private void ClearDiagnostics(LanguageServer* language);
		// Every source's diagnostics of a path, gone (its file is not there any more).
		private void ForgetDiagnostics(Workspace* ws, Core::StringView path);
		// lsp.refresh: clients fetch `kinds` again for `path` (empty: every document).
		private void Refresh(Workspace* ws, Core::StringView path, const Core::Array<Core::String>& kinds);
		private Core::String StatusSnapshot(Workspace* ws) const;
		private bool TextOf(Workspace* ws, Core::StringView uri, Core::String& text);
		private const LineIndex* LinesOf(Workspace* ws, Core::StringView uri);
		// The workspace's files as ied: URIs; another file's canonical file: URI, recorded as named by `language`.
		private Core::String ToClientUri(LanguageServer* language, Core::StringView uri);
		private Core::String ToServerUri(Core::StringView uri) const;
		// The rewrite of a message to (or from) `language`, whose positions without a URI belong to `uri`.
		private Rewrite RewriteFor(LanguageServer* language, Core::StringView uri, bool toServer);
		private Core::String RequestMethod(Call& call, const Params& params);
		private Core::String CapabilitiesMethod(Call& call, const Params& params);
		private Core::String StatusMethod(Call& call, const Params& params);
		private Core::String RestartMethod(Call& call, const Params& params);
		private Core::String WorkspaceSymbolsMethod(Call& call, const Params& params);
		// The symbols one server answers for `query`, for clients: positions in UTF-16, workspace files as ied: URIs.
		private void SymbolsOf(Call& call, LanguageServer* language, Core::StringView query, JsonWriter& json);

		private Server* server;
		private Core::Array<LanguageServer*> languages;
		private Core::HashMap<Core::String, LineIndex*> lineCache;           // by URI, for one message
		private Core::HashMap<Core::String, NamedFile*> named;               // "<wsId>|<file URI>"
		private Core::HashMap<Core::String, bool> scanned;                   // wsIds StartFor has walked
	}

	// A params object's JSON with "textDocument": {"uri": `uri`} in place of any it had.
	Core::String WithTextDocument(Core::StringView params, Core::StringView uri);
	// Code actions cut to those whose diagnostics meet `range` (or that have none), for servers that return them all.
	Core::String FilterCodeActions(const Serialization::DomNode& actions, const Serialization::DomNode& range);
	Core::String LspTopic(const Workspace* ws);
}
