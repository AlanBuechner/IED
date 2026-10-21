# 24 - Language servers

## Purpose

How IED runs language servers (F-EDT-10..24, `docs/05` `src/lsp`): the server is the LSP client, one pool per workspace, so every device sees the same diagnostics and an AI tool asks the same server a person does. Everything language-specific comes from a plugin's `lspProfiles` contribution (`shared/host-api/schemas/plugin-manifest.v1.schema.json`); the host knows no language. The first server is C4LSP through the declarative `c4` plugin that ships with the C4 toolchain (`docs/17`).

## Profiles and selection

A profile names its server (`command`, `args`, `env`, `cwd`; `${pluginDir}`, `${solutionDir}`, `${workspaceDir}`, `${projectName}`, `${projectDir}` expand), which documents it serves (`selector`: language ids, extensions, globs), how many servers it runs (`scope`), how it starts (`launchMode`), and its quirks (`positionEncoding`, `requestIds`, `refetchOnDiagnostics`, `codeActionsNeedClientFilter`). A document is served by the last loaded plugin's profile whose selector matches it (language id from `docs/10`, else extension, else glob) and whose `when` holds for the workspace (`workspaceTrusted`, `workspaceKind`, `config.<setting>`).

**Scope.** `workspace` (the default) runs one server per workspace and profile. `project` runs one per project of a solution workspace (`docs/03`): a document goes to the server of the project whose code root holds it (the deepest), and `${projectName}` / `${projectDir}` name that project in `command`, `args`, `env`, `cwd` and `initializationOptionsFile`. Documents in no project - every document of a folder workspace - share one workspace-wide server with those variables empty. A compiler that analyzes one program per process (C4LSP: its options name one program's build description) needs `project`.

## Lifecycle

- **Lazy.** A server starts when the first document it serves opens in the workspace (`doc.open`), and serves every document of the workspace it matches after that. One process per workspace and profile.
- **Initialize.** `rootUri` and one `workspaceFolders` entry are the workspace folder; `initializationOptions` are the profile's, with the JSON of `initializationOptionsFile` merged over them (its keys win); `capabilities` say what the host and the web client use, with `general.positionEncodings` offering `utf-16` and `utf-8` unless the profile fixes an encoding. The server's capabilities are kept and given to clients.
- **Staged launch** (`launchMode: staged`): the executable's folder is copied to `<data>/lsp-stage/<profile>/<digest>/` and the copy runs, so the original can be rebuilt; when the original changes, the server is restarted from a new copy.
- **Exit.** A server that exits is restarted with a backoff (1 s, 2 s, 4 s); after three exits in five minutes it stays down as `failed`, with the reason in the workspace's IED output and on the `lsp:<wsId>` topic, until `lsp.restart`. Requests waiting on it fail with `plugin_error` ("the language server exited"); the restarted server gets every open document it serves again. Stopping IED (or `lsp.restart`) sends `shutdown` then `exit` and kills a server still running 2 s later; the server processes live in IED's job, so they end with it whatever happens. A server that failed or stopped takes its diagnostics with it.
- **Errors** of a server (its stderr, malformed messages) go to the workspace's IED output source, prefixed with the profile.

## Documents

The host's document store is the truth. When a served document opens, the server gets `didOpen` (text, version, language id); each `doc.edit` (a person's, an AI tool's, a reload from disk) becomes a `didChange` with the full text; saving sends `didSave`. A file of the workspace that is not open is the server's to read from disk. URIs are `file:` URIs of the absolute path. When a workspace's trust changes, servers whose profile's `when` no longer holds stop, and those that now hold start for the open documents.

## Positions

Clients speak UTF-16 code units (`docs/10`). The host converts every position it passes - in requests, results and notifications - to the server's encoding and back, using the open document's text or, for another file, its text on disk: `utf-16` (nothing to do), `utf-8` (UTF-8 bytes on the line, the negotiated or the fixed encoding) and `bytes` (the same, for servers that count bytes without saying so: C4LSP today, `docs/17` L1).

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

## Open items

- Incremental `didChange` for servers that ask for it (C4LSP takes full text).
- `workspace/applyEdit` through the document store as the server's own edit (`origin: lsp`).
- Profiles whose `when` names `config.*` are chosen again when settings change (today: when a document opens and when trust changes).
- A project's server is not stopped when its project leaves the solution; it serves on until IED restarts or `lsp.restart`.
- `settingsBindings` pushed with `didChangeConfiguration`; the `initializationOptionsFile` watched (restart on change).
- `tcp` transport.
