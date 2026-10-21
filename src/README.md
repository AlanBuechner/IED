# src/ - IED server (C4)

The native half of IED: a **standalone background server that owns all state**. The web client (`frontend/`) is only a view (`docs/01`). `ai/prompts/` holds data the server will ship.

## Build, run, check

```bat
nuke build ied-server
build\bin\Development-windows-x64\ied-server\ied-server.exe --static frontend --workspace .
nuke test ied-server
nuke conformance
```

The first stdout line is `LISTENING http://<host>:<port>`, the second `OPEN http://<host>:<port>/?t=<token>`: open that
link within a minute to sign this machine's browser in (`docs/11`). Options: `--host` (default 127.0.0.1; anything
beyond loopback needs an owner password), `--port` (0: a free one), `--static <dir>`, `--data <dir>` (the server's own
state; default `%LOCALAPPDATA%\IED`, with `--test-hooks` a fresh temporary folder), `--workspace <dir>` (repeatable),
`--test-hooks` (the test suites' `testhooks.*`, never in normal use), `--idle-timeout <s>`, `--outbound-budget-mb`,
`--coalesce-kb`, `--allow-origin`, `--allow-host`, `--quiet`. `nuke conformance` builds the server and runs
`tools/conformance` (51 checks) and `tools/server-checks` (79 checks: access rules, trust, file watching, the control pipe,
the CLI, plugins, settings, solutions, outputs, layouts and the web client's files) against it; `nuke e2e` drives the web client against it; `nuke test ied-server ied` runs the unit tests in `tests/` and `../cli/tests/`. All pass. With
no `--port`, a taken 8765 falls back to a free port; `--control <name>` names the control pipe; `--home <dir>` is the install
(`frontend/`, `plugins/`, `shared/`; found above the program by default) and `--app-data <dir>` the app tier.

## What exists (M1, in progress)

| Folder | Files | What |
|---|---|---|
| `host/` | `config`, `server`, `securitylog`, `control`, `trust`, `appdata`, `schemas` | command line and the data folder; the shared state under one lock (epoch, hub, methods, sessions, services); simulated restart; the audit log; the control pipe (`docs/10` "Local control channel": single instance, `open` / `url` / `status` / `stop` for the `ied` CLI in `../cli`); workspace trust in `<app data>/trust.json` (`docs/11`: untrusted until the owner trusts a folder); the app data folder watched (settings and app plugins live) |
| `web/` | `json`, `frames`, `hub`, `outbox`, `rpc`, `rpcerror`, `params`, `session`, `front`, `authgate` | protocol v1 (`docs/10`): frames and their checks, topics with seq and rings, per-client outbox (budget, coalescing, gaps), methods with typed params, stable errors, opId idempotency and cancellation, the WebSocket session (hello/resume, sub/unsub, liveness, a writer thread), HTTP (Host/Origin checks, `/ws`, `/healthz`, uploads and blobs, static files with the content policy, `/ui-kit/` and `/codicons/<name>.svg`), the session cookie, the launch-token exchange and `/api/auth/*` |
| `auth/` | `auth`, `devices`, `throttle` | `docs/11`: launch tokens, device sessions (only the SHA-256 of a cookie is kept), the Argon2id owner password hashed outside the lock, the login throttle, `auth.setPassword` / `auth.devices` / `auth.revoke` |
| `services/` | `presence`, `workspaces`, `documents`, `testhooks`, `blobs`, `outputs`, `layouts`, `search`, `diagnostics`, `terminals`, `trash` | the `server` topic and notifications; folders with `fs.*` and revision checks; server-owned documents with lease and rebase; output sources; the layout documents per workspace and device class (`layout.set`; a solution's in `.ied/local/layout/`, a folder's in the app data); search; every file's problems by source; terminals under a pseudo console; the recoverable trash; the conformance hooks |
| `fs/` | `textedit`, `changes`, `watching`, `glob` | line/column (UTF-16) edits, their rebase, applying them; a watcher thread per workspace folder whose changes are gathered and folded per path, then published unless they were the server's own (recognized by the digest of what it wrote) |
| `plugins/` | `manifest`, `host` | `docs/02` "The host as built": built-in and app plugins found, manifests validated (schema, API range, kind, dependencies), approval by content digest, precedence, enable/disable, `plugins.*` with each UI module's URL, `/plugins/<id>/<path>` and a workspace's `/w/<wsId>/plugins/<id>/<path>` (local plugins included) |
| `solutions/` | `model` | `docs/03` "The server as built": a `.ied.sln` and its `.ied.proj` files (paths and globs) validated, roots resolved, problems per file, local plugins |
| `settings/` | `jsonedit`, `catalog`, `tierfile`, `merge`, `service` | `docs/07` "Settings on the wire": the settings plugins declare, the app and solution tier files checked with problems on their lines (the last valid value staying), effective values per workspace by merge kind, `settings.*` and the `settings:*` topics, edits that keep a file's comments, revision-checked |
| `lsp/` | `client`, `framing`, `pool`, `positions`, `profiles`, `queue`, `rewrite`, `uris` | `docs/24`: language servers from plugins' profiles, one per workspace and profile, requests passed through |
| `tasks/` | `model`, `service`, `runner`, `expand`, `selections`, `matcher` | `docs/06`: what projects offer to run, the toolbar's selections, runs with their steps, problem matchers |
| `dap/` | `providers`, `session`, `breakpoints`, `service` | `docs/25`: debug adapters from plugins (stdio or TCP), sessions, the workspace's breakpoints and watches |
| `ai/` | `wire/`, `step`, `classify`, `models`, `probe`, `permissions`, `prompts`, `tools/`, `run*`, `chat`, `chatstore`, `service` | `docs/04` "As built": the model wire (SSE, streaming requests, completions, dialects), a step under its watchdogs, the stop taxonomy, model profiles and groups, what a server says of itself, permissions, the system message, the core tools, the turn loop with its recoveries (`docs/12`), chats on disk and `chat.*` |
| `tests/` | `*_tests.cf` | unit tests of the honest parts (`nuke test ied-server`) |

Methods run holding the server lock, as the mock's single thread does, and release it only to wait (`Call::Sleep`); each
request runs on a thread of its own, so responses overtake each other; each socket has a writer thread, so a slow
client never holds up a producer.

The web client (`../frontend`, `docs/15`) runs on this server unchanged from the mock (`nuke e2e`). Still to come in M1
(`docs/18`): the 24 h idle soak.

```
src/
+- host/        lifecycle, single instance + named-pipe CLI channel, workspaces (multi), trust, notifications, persistence helpers
+- web/         static assets, HTTP API, auth (loopback token, owner password, devices), WS hub (topics, ring buffers, resume,
|               backpressure, opId idempotency), port-forward proxy                                      - docs/10, docs/11
+- settings/    3-tier settings store, schemas, merge, validation, secrets references                    - docs/07
+- plugins/     discovery, validation, trust/approval, built-in + declarative registration, bindings     - docs/02
+- jsruntime/   QuickJS-ng behind a C shim (JS plugin logic)
+- procplugins/ process plugins over ied-plugin-rpc/1                                                     - shared/host-api/README.md
+- fs/          document store (buffers, leases, rebase, hot-exit journal), watching, ripgrep search, trash
+- tasks/       task/step runner, variables, conditions, problem matchers, output sources, test results  - docs/06
+- terminal/    ConPTY supervisor (bottom terminal, AI terminals, task processes), job objects
+- lsp/         generic LSP client pool (profiles from plugins; encoding/id/refetch quirk switches)
+- dap/         generic DAP client pool, server-owned debug sessions
+- git/         git CLI orchestration (porcelain v2 parsing, locks per repo)
+- ai/          providers + dialects, scheduler/routing/privacy, agent loop + reliability, tools, context ledger +
|  |            compression, persistence, MCP client/server, egress proxy, usage, goals/queue/verifier  - docs/04, 12, 13
|  +- prompts/  base-system.md, style-default.md, harness-notes.md (shipped texts)                      - docs/14
+- spell/       code-aware spell service (diagnostics)
+- profiler/    ITP capture server, chunked trace storage, indexes, analysis queries                     - docs/16
+- sandbox/     Docker / WSL / SSH providers, egress gateway
+- supervisor/  child-process management (LSP/DAP/MCP, SearXNG, tor, Edge CDP, claude bridge, plugin backends)
+- shims/       hand-written C shims (Winsock/IOCP if not in Bedrock, ConPTY, QuickJS, Media Foundation,
                Windows Graphics Capture, Credential Manager) - opaque handles only                       - docs/17
```

Nothing here may contain C4-language or game-engine-specific logic (AGENTS.md rule 1).
