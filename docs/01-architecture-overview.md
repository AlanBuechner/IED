# 01 - Architecture overview

## Purpose

Fix the top-level shape of IED so every later decision has a place to live.

## Core model: split server + web client

IED is a **standalone server** and a **web frontend**. The UI is an ordinary web app that runs in any browser - desktop, phone, another machine on the network. The server owns everything; the browser is a thin, disposable view.

Consequences (load-bearing):

- **Close the browser -> work continues.** AI runs, builds, terminals, debug sessions and LSP servers live on the server. Reopen or switch devices and you resume exactly where you left off (live streams replay from ring buffers; history comes from disk) - `docs/10`.
- **Remote access is first-class**: phone browser, another PC, anywhere the endpoint is reachable (securely - `docs/11`). The UI has real phone layouts (`docs/15`).
- **All state lives on the server**, including unsaved editor buffers, layouts (per device class) and drafts. The browser holds no authoritative state.
- **Multi-workspace:** one server hosts several open workspaces at once (C4 and Claymore can both have agents running); each browser tab views one.
- Multiple clients can attach concurrently, with explicit per-resource conflict rules (`docs/11`).

## Server (`ied-server`, C4)

```
ied-server.exe                   one per user; background; single instance (named-pipe handoff from `ied`)
+- host          lifecycle, workspaces, settings tiers, trust, notifications, persistence helpers
+- web           static assets, HTTP API, auth, WS hub (topics, ring buffers, resume, backpressure), port-forward proxy
+- plugins       manifest validation, built-in plugins (compiled in), declarative plugins, QuickJS-ng JS logic, process plugins
+- fs            document store (server-owned buffers, leases, rebase), file watching, search (ripgrep), trash
+- tasks         task/step runner (toolbar buttons, F5, templates), problem matchers, output sources
+- terminal      ConPTY supervisor (bottom terminal, AI terminals, task processes), job objects
+- lsp / dap     generic client pools (servers and adapters contributed by plugins)
+- git           git CLI orchestration
+- ai            providers + dialects, scheduler/routing, agent loop + reliability, tools, context/compression,
|                MCP client/server, egress proxy, usage, goal queue, verifier
+- spell         code-aware spell service (diagnostics)
+- profiler      ITP capture server, trace storage/indexes, analysis queries
+- sandbox       container / WSL / SSH providers
+- supervisor    child processes: LSP/DAP/MCP servers, SearXNG, tor, Edge (CDP), claude CLI bridge, plugin backends
```

Everything that needs OS access, performance or long-lived processes lives here. It keeps running with zero connected clients - that is the point. The C4 capabilities this requires (sockets, processes, ConPTY, file watching...) are tracked in `docs/17`.

## Client (web app)

Served by the server; one multiplexed WebSocket per tab (+ HTTP for blobs/uploads). Monaco and xterm.js run client-side; LSP, builds, AI, git, docker and profiler analysis stay server-side (the profiler streams level-of-detail data, so phones can browse big captures). Docking/splits are DOM layout - no floating windows. Plugin UI modules load from the server into the page.

## Transport

`docs/10` defines protocol v1: frame types, topics, sequence numbers, snapshots, gaps, resume, backpressure, idempotency, the document editing protocol, and the local named-pipe control channel used by the `ied` CLI.

## Plugin runtimes

Built-in (compiled into the server), declarative (manifest only), JS (QuickJS-ng on the server) and process (JSON-RPC executables) - all with the same contribution model; UI modules run in the browser. Why four and not "C4 DLLs": `docs/02`, `docs/17`.

## Storage

| Where | What |
|---|---|
| `<solution>/.ied/` (committed) | solution/project settings, skills, modes, privacy rules, dictionary, sandbox definitions, evals |
| `<solution>/.ied/local/` (gitignored) | chats, checkpoints, buffers journal, layouts, breakpoints, LSP options from builds, captures |
| `%APPDATA%/IED/` | owner settings, keybindings, global skills/modes/plugins, prompt overrides, generic chats |
| `%LOCALAPPDATA%/IED/` | logs, caches, trash, bundled-service runtime state, trust records, devices |

Details: `docs/03`, `docs/21`.

## Desktop wrapper (optional, later)

A WebView2 window pointed at the local server gives an app feel and receives browser-reserved shortcuts (Ctrl+W/T/N/Tab). Not required for any feature (`docs/15`, `docs/21`).

## What deliberately does NOT exist in the host

- No C4 language logic, no Claymore knowledge, no hardcoded toolchain paths. Language/engine support arrives through plugins, LSP, DAP and MCP.
- No build system. Nuke (or any other) is invoked through tasks (`docs/06`).
- No model weights. "Local AI" means the owner runs model servers; IED connects to them.
