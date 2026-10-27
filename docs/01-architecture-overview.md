# 01 - Architecture overview

## Purpose

Fix the top-level shape of IED so every later decision has a place to live.

## Core model: split server + web client

IED is a **standalone server** and a **web frontend**. The UI is an ordinary web app that runs in any browser - desktop, phone, another machine on the network. The server owns everything; the browser is a thin, disposable view.

Consequences (load-bearing):

- **Close the browser -> work continues.** AI runs, builds, terminals, debug sessions and LSP servers live on the server. Reopen or switch devices and you resume exactly where you left off (live streams replay from ring buffers; history comes from disk) - `docs/10`.
- **Remote access is first-class**: phone browser, another PC, anywhere the endpoint is reachable (securely - `docs/11`). The UI has real phone layouts (`docs/15`).
- **All state lives on the server**, including unsaved editor buffers, layouts (per device class) and drafts. The browser holds no authoritative state.
- **Multi-workspace, a process each:** several workspaces are open at once (the C4 compiler and a game project can both have agents running), each in a server process of its own, under one master server (below); each browser tab views one.
- Multiple clients can attach concurrently, with explicit per-resource conflict rules (`docs/11`).

## Processes: the master and the workspace servers

Owner, 2026-10-26: "multiple servers one for each project that is open and one master server that will manage the servers and tell the front end what server to use". One program, `ied-server.exe`, runs in two roles:

```
ied-server.exe                    the master: one per user, the single instance (control pipe), the only port a browser reaches
+- auth, devices, launch tokens   sign-in for every process (one cookie)
+- the open workspaces            which are open, their ids, the recent projects, notifications, connected tabs ("server" topic)
+- supervisor                     starts, watches, restarts and stops one workspace server per open workspace
+- proxy                          /w/<wsId>/... (HTTP and the workspace's WebSocket) to that workspace's server
+- app-tier writes, app services  settings.json, trust.json, plugins.json; the Claude account; SearXNG and Tor; developer builds
|
+- ied-server.exe --role workspace --ws-id w1 ...   one workspace: its watcher, documents, chats, builds, terminals, LSP/DAP,
+- ied-server.exe --role workspace --ws-id w2 ...   git, browser pages, profiler; loopback only, reached through the master
```

What this buys: a workspace's crash, hang or memory growth (language servers, chats, builds) stays in its process, and one workspace server restarts alone (`ws.restart`, or by itself after a crash: `docs/12` "Restart behavior") while the others go on. The master is small - no workspace, no language server, no chat turn runs in it - so it is the part least likely to fall over; when it stops, its workspace servers stop with it (they run in its job) and come back with it.

**Who owns what** (one owner each; the others reach it as the last column says):

| What | Owner | How the others reach it |
|---|---|---|
| The control pipe (`ied`, the desktop app), the HTTP port, `/` and the web client's files, sign-in, cookies, devices, the owner password, launch tokens, the audit log (`docs/11`) | master | the master proxies `/w/<wsId>/...` to the workspace's server with a secret only it holds (`docs/10` "Routing", `docs/11` "Workspace servers") |
| The open workspaces (`wsId`s, names, kinds, each one's server and its state), the recent projects, notifications, the connected tabs: the `server` topic | master | a workspace server tells the master what changed of its workspace (`master.workspace` on the control pipe) |
| A workspace: its files, watcher, documents, layouts, chats, tasks, terminals, language servers, debug adapters, git, trash moves, browser pages, profiler captures | its workspace server | the client's second socket (`/w/<wsId>/ws`) |
| App-tier files: `settings.json`, `trust.json`, `plugins.json` | the master writes them; every process reads and watches them | a workspace server sends its writes to the master (`master.settings`, `master.trust`); `plugins.setEnabled` and `plugins.approve` are the master's methods |
| The Claude account (`docs/04` "Usage"): the CLI looked for and asked, the usage windows, the sign-in and install windows, setting Claude up, the `aiusage` topic | the chat plugin's code in the master | workspace servers keep a copy: the master binding `ai.claude.account` answers its state (asked every 15 s and at each refresh) and takes what their sessions learn (a usage answer, a rate limit, a sign-in) |
| Web search's SearXNG and Tor (`docs/29`) | their plugins' code in the master | their bindings are master bindings: a `web_search` in a workspace server's chat calls `websearch.search` in the master |
| Developer mode's install build and the relaunch (`docs/28`) | the developer plugin's code in the master | the `ied.developer.install` binding and `IED::RelaunchServer` go to the master |
| A workspace's MCP servers (`docs/04` "MCP"), app-tier definitions included | the MCP plugin's code in that workspace's server | - (each workspace server starts the servers its workspace uses; `mcp:<wsId>` and the `mcp.*` methods name the workspace) |
| The headless browser (Edge, `docs/27`) and its profile | each workspace server, its own (`<data>/servers/<key>/plugins/ied.browser/profile`) | - (a workspace's pages are its own already) |

**Plugins in both roles** (`docs/26` "Servers"): every process loads every plugin's manifest (settings schemas, contributions, UI modules); a plugin's code runs in the roles its `runtime.servers` names - `["workspace"]` unless it says - and asks `PluginContext::Server()` which role it is in. A binding marked `master: true` is answered by the master's copy, and a workspace server with no provider of a binding asks the master's.

## Server (`ied-server`, C4)

```
ied-server.exe                   the master and each workspace server (above); the master is single instance (named-pipe handoff from `ied`)
+- host          lifecycle, roles, workspaces (the master's list, a workspace server's one), the supervisor, settings tiers, trust, notifications, persistence helpers
+- web           static assets, HTTP API, auth, WS hub (topics, ring buffers, resume, backpressure), the master's proxy to workspace servers, port-forward proxy
+- plugins       manifest validation, plugin packages: built-in code compiled in, other plugins' C4 code in the script host (docs/26)
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
+- supervisor    child processes: LSP/DAP/MCP servers, SearXNG, tor, Edge (CDP, plugins/browser, docs/27), claude CLI bridge, plugin backends
```

Everything that needs OS access, performance or long-lived processes lives here. It keeps running with zero connected clients - that is the point. The C4 capabilities this requires (sockets, processes, ConPTY, file watching...) are tracked in `docs/17`.

## Client (web app)

Served by the master; two multiplexed WebSockets per tab - the master's (`/ws`) and the shown workspace's (`/w/<wsId>/ws`, which the master passes to that workspace's server) - routed by the client from the master's `routes` (`docs/10` "Routing"), + HTTP for blobs/uploads. Monaco and xterm.js run client-side; LSP, builds, AI, git, docker and profiler analysis stay server-side (the profiler streams level-of-detail data, so phones can browse big captures). Docking/splits are DOM layout - no floating windows. Plugin UI modules load from the server into the page.

## Transport

`docs/10` defines protocol v1: frame types, topics, sequence numbers, snapshots, gaps, resume, backpressure, idempotency, the document editing protocol, and the local named-pipe control channel used by the `ied` CLI.

## Plugin runtimes

A plugin is a package (`docs/26`): C4 code compiled into the server (built-ins) or compiled and loaded by the server's C4 script host (app and project plugins), configuration, a Python Nuke driver, browser UI modules, skills and prompt sections - all with the same contribution model. C4 scripts share the server's GC, types and reflection, which C4 DLLs could not (`docs/17`).

## Storage

| Where | What |
|---|---|
| `<solution>/.ied/` (committed) | solution/project settings, skills, modes, privacy rules, dictionary, sandbox definitions, evals |
| `<solution>/.ied/local/` (gitignored) | chats, checkpoints, buffers journal, layouts, breakpoints, LSP options from builds, captures |
| `%APPDATA%/IED/` | owner settings, keybindings, global skills/modes/plugins, prompt overrides, generic chats |
| `%LOCALAPPDATA%/IED/` | logs (the master's `logs/server.log`, each workspace server's `logs/workspace-<key>.log`), caches, trash, bundled-service runtime state, devices; a workspace server's own state (its plugins' state folders, its script cache) in `servers/<key>/`, `key` the first 16 hex digits of the SHA-256 of the workspace folder's canonical path |

Details: `docs/03`, `docs/21`.

## Desktop app

`ied-desktop.exe` (`desktop/`, `docs/27`): a native window per IED view with the same web client in WebView2 (the Edge engine Windows ships), starting the user's master server when none runs and again if it dies while windows are open (a workspace server's restart is the master's own business). It receives browser-reserved shortcuts (Ctrl+W/T/N/Tab). It is a view like any browser tab; no feature depends on it. The same engine runs on the server, headless, for the AI's browser tools and the Browser tab (`plugins/browser`).

## What deliberately does NOT exist in the host

- No C4 language logic, no game-engine knowledge, no hardcoded toolchain paths. Language/engine support arrives through plugins, LSP, DAP and MCP.
- No build system. Nuke (or any other) is invoked through tasks (`docs/06`).
- No model weights. "Local AI" means the owner runs model servers; IED connects to them.
