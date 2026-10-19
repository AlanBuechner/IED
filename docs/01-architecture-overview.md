# 01 — Architecture overview

## Purpose

Fix the top-level shape of IED so every later decision has a place to live.

## Core model: split server + web client

IED is a **standalone server** and a **web frontend**. The UI is an ordinary web app that runs in any browser — desktop, phone, another machine on the network. The server owns everything; the browser is a thin, disposable view.

Consequences (these are load-bearing):

- **Close the app → work continues.** AI chats, builds, terminals and LSP servers all live on the server. Reopen or reconnect and you resume exactly where you left off (live streams replay from a ring buffer; history comes from disk).
- **Remote access is first-class**, not an afterthought: phone browser, another PC, anywhere the HTTP(S)+WS endpoint is reachable. The UI must work at phone widths.
- **All state lives on the server.** The browser holds no authoritative state; refresh or switch devices and everything (open tabs, layout, running sessions) restores from the server.
- Multiple clients can attach to the same workspace concurrently (concurrency semantics: 99 #13).

## Server (`ied-server`, C4 native)

```
ied-server.exe                     standalone background process (foreground / start-minimized; service install later)
├── Web server                     static assets + HTTP API + one multiplexed WebSocket hub per client
├── Plugin host                    in-process C4 plugins; QuickJS-ng runtime for JS plugin logic
├── LSP client pool                one connection per registered LSP server (stdio or socket)
├── DAP client pool                one connection per debug session
├── AI runtime                     model connections, streaming, tool executor, compression, egress proxy
└── Process supervisor             terminal PTYs, nuke builds, docker, MCP servers
```

Everything that needs OS access, performance, or long-lived processes lives here. It keeps running with zero connected clients — that is the point.

## Client (web app, html/css/js)

Served by the server over HTTP; talks to it over **one multiplexed WebSocket**. Must run well in a phone browser. Monaco and xterm.js execute client-side; all heavy lifting (LSP, builds, AI inference plumbing, git, docker) stays server-side. No free-floating windows — docking/splits are DOM layout inside the page.

## Transport

- **HTTP:** static assets + simple request/response ops (settings get/set, plugin list, workspace open…).
- **One multiplexed WebSocket per client:** typed JSON frames — request/response with `id`, push events by topic (`ai.stream`, `build.output`, `terminal.io`, `lsp.diagnostics`, `fs.changed`, `toolcall.status`…), streaming chunks, cancellation.
- **Reconnect/resume:** the server keeps a ring buffer of recent events per session; on (re)connect the client requests resume → replays missed events + reconciles full state. This is what makes "close the app and come back later" seamless.

## Plugin runtime split

- **Server side:** C4 plugins + JS plugin *logic* (tool calls, bindings, LSP/DAP glue, skills) run in-process / QuickJS-ng on the server.
- **Client side:** plugin *UI* contributions are web modules (js/css/html) that the shell loads from the server — same contribution model as any other UI, but executed in the browser. See `docs/02`.

## Desktop wrapper (optional, later)

A native window embedding a webview (WebView2 — already vendored in `third_party/`) pointed at the local server gives an "installed app" feel with one-click launch. It is **not required for any feature**; v1 ships as: start server → open browser. (This resolves the old webview question: no embedded webview in v1.)

## What deliberately does NOT exist in the host

- No C4 language logic. No Claymore knowledge. No hardcoded toolchain paths beyond what Nuke's `Config.py` already owns.
- No build system. Nuke is the build system (see 06).
- No AI model weights. IED talks to configured model endpoints (local or remote) — "local AI" means *the user runs the models locally*, not that IED bundles them.

## Open items

- WS frame protocol v1, incl. resume semantics (99 #2).
- Remote access security: auth for non-localhost clients, default bind address, TLS story (99 #13).
- Multi-client concurrency semantics (99 #13).
