# src/ - IED server (C4)

The native half of IED: a **standalone background server that owns all state**. The web client (`frontend/`) is only a view (`docs/01`). `ai/prompts/` holds data the server will ship.

## Build, run, check

```bat
nuke build ied-server
build\bin\Development-windows-x64\ied-server\ied-server.exe --static frontend --workspace .
nuke conformance
```

The first stdout line is `LISTENING http://<host>:<port>`; options: `--host` (default 127.0.0.1), `--port` (0: a free one),
`--static <dir>`, `--workspace <dir>` (repeatable), `--test-hooks` (the conformance suite's `testhooks.*`, never in
normal use), `--idle-timeout <s>`, `--outbound-budget-mb`, `--coalesce-kb`, `--allow-origin`, `--allow-host`, `--quiet`.
`nuke conformance` builds the server and runs `tools/conformance` against it (all 46 checks pass).

## What exists (M1, in progress)

| Folder | Files | What |
|---|---|---|
| `host/` | `config`, `server` | command line; the shared state under one lock (epoch, hub, methods, sessions, services); simulated restart |
| `web/` | `json`, `frames`, `hub`, `outbox`, `rpc`, `rpcerror`, `params`, `session`, `front` | protocol v1 (`docs/10`): frames and their checks, topics with seq and rings, per-client outbox (budget, coalescing, gaps), methods with typed params, stable errors, opId idempotency and cancellation, the WebSocket session (hello/resume, sub/unsub, liveness, a writer thread), HTTP (Host/Origin checks, `/ws`, `/healthz`, uploads and blobs, static files) |
| `services/` | `presence`, `workspaces`, `documents`, `testhooks`, `blobs` | the `server` topic and notifications; folders with `fs.*` and revision checks; server-owned documents with lease and rebase; the conformance hooks |
| `fs/` | `textedit` | line/column (UTF-16) edits, their rebase, applying them |

Methods run holding the server lock, as the mock's single thread does, and release it only to wait (`Call::Sleep`); each
request runs on a thread of its own, so responses overtake each other; each socket has a writer thread, so a slow
client never holds up a producer.

Still to come in M1 (`docs/18`): auth (loopback token, owner password), settings tiers, the plugin host (built-in and
declarative), file watching, single instance and the named-pipe CLI, persistence and the IED output source.

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
