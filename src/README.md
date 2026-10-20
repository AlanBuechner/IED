# src/ - IED server (C4)

The native half of IED: a **standalone background server that owns all state**. The web client (`frontend/`) is only a view (`docs/01`). C4 sources arrive when the compiler and Bedrock can support them (`docs/17` readiness checklist); `ai/prompts/` already holds data the server will ship.

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
