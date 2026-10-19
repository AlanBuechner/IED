# 01 — Architecture overview

## Purpose

Fix the top-level shape of IED so every later decision has a place to live.

## The two halves

IED is **native host + web frontend**:

- **Host (C4, native):** application shell, window management, plugin runtime, IPC server, settings store, LSP/DAP client infrastructure, AI model clients (streaming inference over HTTP/sockets), terminal process spawning, file watchers, git plumbing, docker plumbing. Anything that needs OS access, performance, or long-lived processes lives here.
- **Frontend (html/css/js):** everything the user sees and touches — layout, panels, Monaco editor, chat UI, trees, output consoles, settings forms. Runs in an embedded webview inside the host.

The frontend never talks to the OS directly; it calls host APIs over IPC. The host never renders anything itself.

## Process model (working assumption — confirm in design)

```
ied.exe                          single native process
├── UI: embedded webview(s)      main window = one webview; split panes are DOM, not windows
├── Plugin host                  in-process plugin API surface for C4 plugins
│   └── JS plugin runtime        bundled JS engine for JS plugins
├── IPC server                   single loop dispatching webview + plugin calls
├── LSP client pool              one connection per registered LSP server (stdio or socket)
├── DAP client pool              one connection per debug session
├── AI runtime                   model connections, streaming, tool-call executor, compression
└── Process supervisor           terminals, build processes (nuke), docker, MCP servers
```

Key constraint from the plan: **no free-floating chat windows** — docking/splitting is DOM layout inside one window. That keeps the webview story simple (one main webview).

## IPC protocol sketch

- One bidirectional channel between webview and host. JSON messages; requests get an `id`, responses echo it; events are push-only with a topic name.
- Method names are **plugin-namespaced**: `chat.openTab`, `editor.revealFile` — so plugins can call other plugins' exposed methods without coupling to their internals (see 02, bindings).
- The same channel is what C4 plugins use from the native side; the JS runtime gets an async mirror of it.
- **Open question:** exact framing/serialization and whether LSP/DAP traffic gets proxied through IPC or a dedicated side channel (perf for token streaming + diagnostics). See 99.

## Module map (first cut)

| Area | Location | Notes |
|---|---|---|
| App lifecycle, window, webview embed | `src/host/` | C4 sources; entry point |
| IPC server + dispatch | `src/ipc/` | protocol lives in `shared/host-api/` so both sides code against it |
| Settings store (3 tiers) | `src/settings/` | see 07 |
| Plugin discovery/validation/loading | `src/plugins/` | manifest v1, enable/disable, trust |
| JS plugin runtime | `src/jsruntime/` | vendored engine + host API bindings |
| LSP client pool | `src/lsp/` | generic; servers registered by plugins (LSP profiles) |
| DAP client pool | `src/dap/` | built-in support per the plan; adapters registered by plugins |
| AI runtime | `src/ai/` | model groups, streaming, tool executor, compression, egress proxy |
| Terminal supervisor | `src/terminal/` | PTY/process handling for bottom shell + AI shell tool |
| Git plumbing | `src/git/` | wraps git CLI/libgit2; UI is the git plugin |
| Docker/sandbox | `src/sandbox/` | container lifecycle for cross-OS builds |
| File watching, FS tools | `src/fs/` | backs editor tree + AI file tools |
| Webview shell (DOM app) | `frontend/src/` | layout engine, panel registry, theme CSS |
| Shared UI components | `shared/ui-kit/` | tree view, tab strip, ANSI console renderer, icon set, context menus |
| Protocol + API types | `shared/host-api/` | the contract; versioned |

## Webview choice — open question

Candidates: WebView2 (Windows-first, matches "Visual Studio on Windows" positioning), embedded Chromium (cross-platform but heavy). The sandbox plugin wants docker for *other* OSes, not necessarily a cross-platform IED binary. **Tentative default: WebView2.** See 99.

## What deliberately does NOT exist in the host

- No C4 language logic. No Claymore knowledge. No hardcoded toolchain paths beyond what Nuke's `Config.py` already owns.
- No build system. Nuke is the build system (see 06).
- No AI model weights. IED talks to configured model endpoints (local or remote) — "local AI" means *the user runs the models locally*, not that IED bundles them.

## Open items

- Webview choice (99).
- IPC framing + streaming channel design (99).
- Whether plugins can run out-of-process for isolation, or in-process only (v1: in-process; revisit if a plugin misbehaves badly).
