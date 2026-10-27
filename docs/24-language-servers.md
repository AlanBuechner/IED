# 24 - Language servers

## Purpose

How IED runs language servers (F-EDT-10..24, `docs/05` `src/lsp`): the server is the LSP client, one pool per workspace, so every device sees the same diagnostics and an AI tool asks the same server a person does. Everything language-specific comes from a plugin's `lspProfiles` contribution (`shared/host-api/schemas/plugin-manifest.v1.schema.json`); the host knows no language. The first server is C4LSP through the declarative `c4` plugin that ships with the C4 toolchain (`docs/17`).

## Profiles and selection

A profile names its server (`command`, `args`, `env`, `cwd`; `${pluginDir}`, `${solutionDir}`, `${workspaceDir}`, `${projectName}`, `${projectDir}` expand, and `${config:<setting>}` to the setting's effective value for the workspace - a string as it is, a number or a boolean as written, anything else or an unset setting as nothing; an argument that is exactly one `${config:...}` of a list setting becomes one argument per element), which documents it serves (`selector`: language ids, extensions, globs), how many servers it runs (`scope`), how it starts (`launchMode`), and its quirks (`positionEncoding`, `requestIds`, `refetchOnDiagnostics`, `codeActionsNeedClientFilter`). A document is served by the last loaded plugin's profile whose selector matches it (language id from `docs/10`, else extension, else glob) and whose `when` holds for the workspace (`workspaceTrusted`, `workspaceKind`, `config.<setting>`, `workspaceContains.<name>`, `workspaceLanguage.<id>`; `src/plugins/context.cf`, `docs/26`).

**Scope.** `workspace` (the default) runs one server per workspace and profile. `project` runs one per project of a solution workspace (`docs/03`): a document goes to the server of the project whose code root holds it (the deepest), and `${projectName}` / `${projectDir}` name that project in `command`, `args`, `env`, `cwd` and `initializationOptionsFile`. Documents in no project - every document of a folder workspace - share one workspace-wide server with those variables empty. A compiler that analyzes one program per process (C4LSP: its options name one program's build description) needs `project`.

## Lifecycle

- **Lazy.** A server starts when the first document it serves opens in the workspace (`doc.open`), and serves every document of the workspace it matches after that. One process per workspace and profile.
- **Initialize.** `rootUri` and one `workspaceFolders` entry are the workspace folder; `initializationOptions` are the profile's, with the JSON of `initializationOptionsFile` merged over them (its keys win), and with `pluginCodeOption` the workspace's plugin code scopes under that key (`docs/26` "Editing plugin code"); `capabilities` say what the host and the web client use, with `general.positionEncodings` offering `utf-16` and `utf-8` unless the profile fixes an encoding. The server's capabilities are kept and given to clients.
- **Staged launch** (`launchMode: staged`): the executable's folder is copied to `<data>/lsp-stage/<profile>/<digest>/` and the copy runs, so the original can be rebuilt; when the original changes, the server is restarted from a new copy.
- **Exit.** A server that exits is restarted with a backoff (1 s, 2 s, 4 s); after three exits in five minutes it stays down as `failed`, with the reason in the workspace's IED output and on the `lsp:<wsId>` topic, until `lsp.restart`. Requests waiting on it fail with `plugin_error` ("the language server exited"); the restarted server gets every open document it serves again. Stopping IED (or `lsp.restart`) sends `shutdown` then `exit` and kills a server still running 2 s later; the server processes live in IED's job, so they end with it whatever happens. A server that failed or stopped takes its diagnostics with it.
- **Errors** of a server (its stderr, malformed messages) go to the workspace's IED output source, prefixed with the profile.

## Documents

The host's document store is the truth. When a served document opens, the server gets `didOpen` (text, version, language id); each `doc.edit` (a person's, an AI tool's, a reload from disk) becomes a `didChange` with the full text; saving sends `didSave`. A file of the workspace that is not open is the server's to read from disk. URIs are `file:` URIs of the absolute path. When a workspace's trust changes, servers whose profile's `when` no longer holds stop, and those that now hold start for the open documents.

## Positions

Clients speak UTF-16 code units (`docs/10`). The host converts every position it passes - in requests, results and notifications - to the server's encoding and back, using the open document's text or, for another file, its text on disk: `utf-16` (nothing to do), `utf-8` (UTF-8 bytes on the line, the negotiated or the fixed encoding) and `bytes` (the same, for servers that count bytes without saying so). C4LSP negotiates `utf-8` since `docs/17` L1.

Positions are found wherever LSP puts them: any object with numeric `line` and `character` is a position of the document the nearest `uri` / `targetUri` / `textDocument.uri` names, or of the request's document. URIs of the workspace's files become `ied://<wsId>/<relative path>` for clients (Monaco models use the same URIs); others stay `file:` URIs, and the host records each one with the server that named it: a client may open that file read-only (`docs/10` "Files outside the workspace"), and the document is served by that server (`didOpen` with the same URI).

## Diagnostics

`textDocument/publishDiagnostics` lands on the `diag:<wsId>` topic: the snapshot is `{files: {<path>: {<source>: [diagnostic]}}}` and each publish is `diag.set {path, source, items}` with `source` the profile id (`<profile>/<project>` for a project's server) and every range in UTF-16. A server that lacks `*/refresh` requests and names kinds in `refetchOnDiagnostics` makes the host publish `lsp.refresh {path, kinds}` on `lsp:<wsId>` after diagnostics, so clients fetch semantic tokens and folding again.

## Methods and topics

| | |
|---|---|
| `lsp.request {docId, method, params}` | sends a request about a document to its server: `params` without `textDocument` (the host adds it), positions UTF-16; the result comes back with positions UTF-16 and workspace URIs as `ied://`. `not_found` when no server serves the document, `unavailable` while it starts or after it failed, the server's own error as `plugin_error {code, message}`. A `cancel` frame sends `$/cancelRequest`. With `codeActionsNeedClientFilter`, the code actions of `textDocument/codeAction` are cut to those whose diagnostics meet the requested range. |
| `lsp.capabilities {docId}` | `{profile, state, capabilities}`: the serving profile and the server's capabilities, waiting for the server to start (20 s at most); `{profile: null}` when no server serves the document |
| `lsp.workspaceSymbols {wsId, query}` | `{symbols: [...]}`: `workspace/symbol` asked of every running server of the workspace that offers it, each symbol with its `source` (as diagnostics name it), positions in UTF-16 and workspace URIs as `ied://`; a server that fails or times out is left out |
| `lsp.status {wsId}`, `lsp.restart {wsId, profile, project?}` | `{servers: [{profile, label, plugin, project, state: starting\|running\|failed\|stopped, pid, restarts, problem}]}` (`project` empty for a workspace-wide server); a restart starts it again |
| topic `lsp:<wsId>` | snapshot `{servers}`; events `lsp.state {profile, project, state, problem}`, `lsp.refresh {path, kinds}` (`path` null: every document, after a server's `workspace/<kind>/refresh`) |
| topic `diag:<wsId>` | as above |

Server-initiated requests are answered by the host: `workspace/configuration` with a null per item (until `settingsBindings` are bound), `workspace/workspaceFolders` with the workspace folder, `workspace/<kind>/refresh` by publishing `lsp.refresh`, `workspace/applyEdit` refused (`applied: false`) for now, and anything else (registrations, `window/workDoneProgress/create`) with null. `window/logMessage`, `window/showMessage` and the beginning and end of `$/progress` go to the workspace's IED output.

## Plugin code asks language servers (2026-10-21)

The AI's code tools (`docs/13` `code_*`) ask the same servers a person's editor does, through `src/api/lsp.ch` (permission `lsp`): a request about a file, a request without a document (a call or type hierarchy item carries its own URI), the workspace's symbols, the text of a file a server named, and what follows an AI write ("After an AI edit" below: `LspAiWrite`, `LspPublishMark`, `LspDiagnosticsAfter`, `LspFormatted`). Results come as clients get them: positions in UTF-16, workspace files as `ied://` URIs, others as `file:` URIs recorded as named.

- **A file nobody has open.** A request about a file that is not an open document still needs it open on its server, but the document store is not burdened with it (a store document never closes) and a server like clangd keeps an AST per open file. So the pool opens it on the server alone - a *transient* open, `didOpen` with the text on disk - and keeps the last 16 per server, closing the oldest with `didClose`. Before each use it compares the file's revision on disk with the one it sent and sends the new text (`didChange`) when it moved; when the store opens the file later, the store's document takes the server's copy over (a full-text `didChange`, then it is the store's). A file outside the workspace is asked of the server that named it, opened the same way under its `file:` URI.
- **Servers nobody started.** `workspace/symbol` reaches only running servers, and a person may not have opened a file of every language yet. For plugin code the pool first starts the servers the workspace's files call for: one walk of the workspace (build output, `.git` and the default excludes skipped; at most 20 000 files, once per workspace and server run) maps each file to its profile and, for a project-scoped profile in a solution, its owning project (files outside every project start none); each server that is missing starts, and the request waits up to 20 s in all for them to run.

## After an AI edit (2026-10-26)

Owner (item 32 of `docs/30`): "auto format and return LSP errors on file edit. this is something that c4 and go should
support but other languages might not have the full tooling for this so it is up to the language plugins to configure.
LSP failure is not a tool call failure. it is to be expected that things will brake mid edit when the edits spand
multiple files." As built (`plugins/chat/code/afterwrite.cf`, `files.cf` `Written`; the pool's side in
`src/lsp/aiwrite.cf`):

- **The language plugin decides.** Two options of an `lspProfiles` entry, each `true` or a when clause that must hold in
  the workspace (`docs/26` section 8): `formatOnAiWrite` - a file the AI writes (`fs_write`, `fs_edit`) is formatted
  with `textDocument/formatting` before the call returns - and `diagnosticsOnAiWrite` - the call's result carries what
  the server then says of the file. Shipped: C4LSP (`C4/IedPlugin`) and gopls (`ied.go`) both; clangd (`ied.cpp`)
  diagnostics, and formatting only where the workspace's root has a `.clang-format` (`workspaceContains..clang-format`;
  without one clangd would reformat to LLVM style). A language whose profile names neither is untouched.
- **The owner turns them off:** `ai.edits.formatOnWrite`, `ai.edits.diagnosticsOnWrite` (app, solution; on), and
  `ai.edits.diagnosticsWaitMs` (default 1500, at most 10000): how long a write waits for the diagnostics, and for a
  server that is still starting.
- **Formatting.** The AI's text is written (through the document store, as every AI write), the server formats the file
  as edited (`IED::LspFormatted`: the indentation the text itself uses - tabs, or spaces and their width - as
  `tabSize`/`insertSpaces`; its TextEdits applied in UTF-16 after the usual conversion from the server's encoding) and
  the formatted text is written over it when it differs. The result says "Formatted by C4LSP: it changed N of the lines
  you wrote, so read them again before an fs_edit near them", and the card's diff is the whole change. A server that is
  not running within the wait, does not format, or fails leaves the file as written and the result says why ("Not
  formatted: ..."); that is no failure either.
- **Diagnostics.** A mark of the file's publications (`IED::LspPublishMark`) is taken before the write (and again before
  the formatted text is written), then `IED::LspDiagnosticsAfter` makes sure the server has the file as it is now (the
  document's, or a transient open) and waits for a publication past the mark - one for an older version than the server
  was sent (`publishDiagnostics.version`) does not count. The result gets "Language server (C4LSP): 2 errors, 1 warning
  in this file - what the code says now, not a failure of this call: while an edit spans several files, errors from the
  files not yet changed are expected." with up to 8 `line L:C: severity: message` lines, then "Other files it finds
  problems in: src/b.cf (2 errors), ..." (the five with the most errors, and how many more). A server that says
  nothing in time, is starting or is down is said so, with `problems_list` to look later.
- **Never a failure.** The call's result is not an error whatever the server says (the card stays green); the
  developer-mode failure note (`docs/28`) is not added to it.

## Open items

- Incremental `didChange` for servers that ask for it (C4LSP takes full text).
- `workspace/applyEdit` through the document store as the server's own edit (`origin: lsp`).
- Profiles whose `when` names `config.*` are chosen again when settings change (today: when a document opens and when trust changes).
- A project's server is not stopped when its project leaves the solution; it serves on until IED restarts or `lsp.restart`.
- `settingsBindings` pushed with `didChangeConfiguration`; the `initializationOptionsFile` watched (restart on change).
- `tcp` transport.
