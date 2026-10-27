// Language servers through the host (docs/24 "Plugin code asks language servers", docs/26): plugin code asks the server
// that serves a file what a person's editor would - the server started and the file opened on it when they are not yet
// - and gets the answer as clients do: positions in UTF-16, the workspace's files as ied: URIs, other files as file:
// URIs. `path` is a workspace path, or the file: URI of a file outside the workspace that one of its servers named. Each
// needs the "lsp" permission; a file's text of the workspace needs "fs.read".

include "Workspace.ch"

namespace IED
{
	// `method` with `params` (JSON) to the server serving `path`; `withDocument` sets the params' textDocument to the file
	// (false for a request that carries its own item, callHierarchy/incomingCalls). The result's JSON, or false with
	// `problem`: no server serves the file, it does not run, or its error.
	bool LspRequest(const Workspace& ws, Core::StringView path, Core::StringView method, Core::StringView params, bool withDocument, Core::String& result, Core::String& problem);
	// {profile, capabilities, aiRequests} of the server serving `path`, started; {profile: null} when none does.
	bool LspServing(const Workspace& ws, Core::StringView path, Core::String& result, Core::String& problem);
	// workspace/symbol of every server of `ws` - the servers its files call for started first, the first time - as a
	// JSON array of SymbolInformation / WorkspaceSymbol, each with its `source`.
	bool LspWorkspaceSymbols(const Workspace& ws, Core::StringView query, Core::String& result, Core::String& problem);
	// The text of `uri`: a file of `ws` as edited (its ied: URI), or a file outside it a server named (a file: URI).
	bool LspFileText(const Workspace& ws, Core::StringView uri, Core::String& text, Core::String& problem);
	// The workspace path an ied: URI of `ws` names; false for another URI.
	bool WorkspacePathOf(const Workspace& ws, Core::StringView uri, Core::String& path);
	// The absolute path a file: URI names; false for another URI.
	bool LocalPathOf(Core::StringView uri, Core::String& path);
	// The canonical file: URI of an absolute path, as servers' answers name a file outside the workspace.
	Core::String FileUriOfPath(Core::StringView path);

	// After an AI write of a file (docs/24 "After an AI edit"), as its language's plugin asks.
	// What the profile serving `path` asks for, whether or not its server runs: {profile, label, format, diagnostics}
	// (its formatOnAiWrite and diagnosticsOnAiWrite); {profile: null} when none serves it.
	bool LspAiWrite(const Workspace& ws, Core::StringView path, Core::String& result, Core::String& problem);
	// How many times the workspace's servers have published `path`'s diagnostics: the mark LspDiagnosticsAfter waits
	// past, taken before the write.
	int64 LspPublishMark(const Workspace& ws, Core::StringView path);
	// What the server serving `path` says of it once it has the file as it is now (started, the file opened on it when no
	// document holds it), waiting at most `ms` for a publication after `mark`: {profile, label, state: fresh | stale |
	// starting | down, items: [diagnostic, UTF-16 positions], elsewhere: [{path, errors, warnings}], elsewhereFiles};
	// {profile: null} when none serves it. Without the lock: it waits.
	bool LspDiagnosticsAfter(const Workspace& ws, Core::StringView path, int64 mark, int64 ms, Core::String& result, Core::String& problem);
	// `path`'s text as edited, formatted by the server serving it (textDocument/formatting with `tabSize` and
	// `insertSpaces`), not written; false with `problem` when no server serves it, it does not run within `ms`, does not
	// format or failed. Without the lock.
	bool LspFormatted(const Workspace& ws, Core::StringView path, int64 tabSize, bool insertSpaces, int64 ms, Core::String& formatted, Core::String& problem);
}
