# 99 - Open questions and decision record

Decide open items during design, then **move the answer into the relevant doc and record it here** (one line, so history stays visible). Items marked *owner* are genuinely the owner's call; the rest have a recommended answer the implementer may adopt unless the owner objects.

## Resolved

| # | Question | Decision | Where |
|---|---|---|---|
| 1 | UI delivery model | Split server + served web client; plus the desktop app (owner, 2026-10-21): `ied-desktop`, a C4 WebView2 window on the same web client that starts the server | `docs/01`, `docs/21`, `docs/27` |
| 2 | Server <-> client protocol v1 | One WS per tab; req/res + topic events with seq + snapshots + gaps; ring-buffer resume; backpressure; idempotent `opId`; doc lease + rebase | `docs/10`, `shared/host-api/protocol/` |
| 3 | Plugin manifest v1 | Drafted as JSON Schema with four runtimes (builtin, declarative, js, process) and 40+ contribution points; frozen after owner review | `docs/02`, `shared/host-api/schemas/plugin-manifest.v1.schema.json` |
| 4 | Project/solution schemas + toolbar grammar | Drafted as JSON Schema; variables use `${name}`; tasks = steps (run/task/debug/binding) with `onlyIf`/`when`, output targets, matchers; federations added | `docs/03`, `docs/06`, `shared/host-api/schemas/` |
| 5 | C4 compiler readiness | Gap analysis done; readiness checklist of 10 items; C4-tree work packages P-1..P-7 | `docs/17` |
| 6 | Plugin code runtime | Owner, 2026-10-21: plugin server logic is C4 code, compiled and loaded by the server's C4 script host (built-ins compiled in); QuickJS-ng and `ied-plugin-rpc/1` process plugins are dropped; AI tool calls are C4 functions or commands; Nuke drivers are Python, UI modules JavaScript | `docs/26` |
| 7 | Context compression details | Ledger + tiered compression (mechanical first, then `summarizer` role); cache-aware (append-only between large compressions); 70% warn / 85% compress defaults; history tools | `docs/04`, `docs/12` |
| 8 | SearXNG packaging | Bundled with embeddable Python + waitress - spike done 2026-10-21, it runs on Windows; tor bundled. Owner, 2026-10-21: web search, web fetch and Tor are plugins (`ied.websearch`, `ied.webfetch`, `ied.tor`), and Tor carries web search only | `docs/21`, `docs/29` |
| 9 | Profiler trace format | ITP v1: chunked varint event stream, explicit string interning, timestamped thread renames, task events, GPU calibration, streamed to `.iedtrace` | `docs/16` |
| 10 | Legacy workspace migration | The predecessor IDE's workspace folder stays in place; IED never writes it; Nuke keeps writing it for now but nothing depends on the predecessor; `ied import legacy` converts settings/skills/modes/chats | `docs/17` (N9), `docs/20` |
| 11 | Claude integration shape | Two providers planned: Anthropic API (API key; dropped by #24) and a bridge that drives the owner's own installed, self-signed-in `claude` CLI in stream-json mode with IED tools via MCP; IED never touches Claude credentials (Anthropic policy) | `docs/04` |
| 12 | Request routing policy | Role fallback lists; failure-class triggers; slot-aware scheduler with interactive priority; privacy always wins (queue rather than leak); capability matching; scorecards optional | `docs/04`, `docs/features/08`, `docs/features/16` |
| 13 | Remote access security and concurrency | Loopback default; Host/Origin checks; loopback one-time token; owner password required for LAN; device sessions; TLS via Tailscale (built-in TLS P2); explicit per-resource concurrency rules | `docs/11` |
| 14 | Git backend | git CLI for everything (porcelain v2 / NUL-separated formats); libgit2 stays vendored for a possible graph optimization later | `docs/features/09-git.md` |
| 15 | Ambition level | Owner: not just parity with the predecessor IDE but drastically surpass it - predecessor parity is the floor; orchestration area added; differentiators raised to P1 | `docs/09`, `docs/20`, `docs/features/16` |
| 16 | Syntax highlighting colors (was O4) | Owner: use the Visual Assist X scheme for code. Default palette = the old C4 editor plugin's `c4-visual-assist-dark` values; a Grayscale palette stays as a per-user option | `docs/07`, `frontend/css/theme.css` |
| 17 | GPU debugger look | Owner: PIX's layout and styling, not RenderDoc's - capture as a document with view tabs, timing timeline on top, events grid, Pipeline state tree beside a resource viewer, docked grids; RenderDoc stays a backend and a capability reference | `docs/22`, `frontend/mocks/07-09` |
| 18 | Where C4 and Nuke live | Owner: in this repository (`C4/`, `Nuke/`), versioned together with IED so an agent can fix the compiler or the IDE and continue; Frost folded in; old history dropped | `README.md`, `AGENTS.md` |
| 19 | Nuke's project format and extensibility | Owner: declarative TOML `nuke.toml` plus optional Python (`scripts`, `python =` steps); every command has pre-/post- phases for project steps; workspaces add commands; commands chain (`nuke build run dev`) | `docs/23`, `Nuke/README.md` |
| 20 | How projects get the compiler | One shared store of immutable, versioned C4 toolchains in this checkout (`toolchains/c4/<version>`, plus `dev` = the in-tree build); workspaces pin a version; nothing vendors Nuke or C4 | `docs/23` N-7 |
| 21 | Prefill-progress signals (was D4) | Verified 2026-10-21 against llama.cpp b10222: `"return_progress": true` on a streamed `/v1/chat/completions` request makes it send, before the first token, chunks with `prompt_progress {total, cache, processed, time_ms}` (and an empty assistant delta, content null). The chat asks for it from servers its probe found to be llama.cpp; each report extends the prefill watchdog and the clients see the prompt read so far (`turn.progress`). vLLM and Ollama send nothing before the first token: the estimate and SSE keepalives cover them | `docs/12` "Watchdogs", `docs/10` |
| 22 | The browser engine for the desktop app, the AI's browser tools and the in-IDE browser | Owner, 2026-10-21: one engine, Edge's - WebView2 in the desktop app, Edge headless over CDP on the server for pages the AI and people share; the Browser tab is server-rendered (pulled frames, forwarded input), so it works on phones and shows the AI's pages live; everything in C4 (the desktop's COM callbacks too) | `docs/27` |
| 23 | GPU debugger backend (was O11) | Owner, 2026-10-21: our own backend, Flashbang (capture library, Vulkan layer, replayer, `.flash` format), with RenderDoc as a reference rather than a dependency - fewer dependencies and full control (bindless, work graphs, engine metadata, time travel); D3D12 capture is opt-in by the program (Bedrock `Gpu::Capture`, `flashbang_app.h`), Vulkan through the loader's layer variables; the only new download is the pinned Khronos Vulkan-Headers | `docs/22` |
| 24 | Claude subscription use (was O2) | Owner, 2026-10-22: Claude only on the owner's Pro/Max subscription ("i only care about using the max/pro subscriptions"), no API-key provider; IED drives the owner's own signed-in Claude Code CLI and never handles credentials, and makes it easy within the terms - one-click Install and Sign in that open the CLI's own windows, the Claude models set up by themselves. Distributed (`Dist`) builds leave it out unless built with `CLAUDE_SUBSCRIPTION_APPROVED` (Anthropic's terms: no claude.ai login in a third-party product without its approval) | `docs/04` |
| 25 | Plugin files, AI edits and failing calls (owner's items 23, 32, 33) | Owner, 2026-10-26: a plugin's skills and docs are readable to every chat as `plugins:/<plugin id>/` and write-locked unless developer mode is on (then written in the source); a language plugin opts its LSP profile into formatting AI writes and reporting their diagnostics - information, never a failed call; in developer mode every failing call carries a note to fix its cause in IED | `docs/13` "Plugin folders", `docs/24` "After an AI edit", `docs/28` items 1 and 5 |

## Open - owner's call

| # | Question | Recommendation |
|---|---|---|
| O1 | **"Federated projects"** - is it a set of solutions/repos opened together as one workspace (e.g. this repo with IED + C4, plus a game project), as designed in `docs/03`? Or something else (e.g. a remote project on another machine)? | As designed |
| O3 | **OS primitives for the server**: build them as Bedrock modules (`Net::`, `Process::`, `FS::Watch`, `Time::`) in the C4 tree (`C4/Bedrock/`), or wrap libuv behind a C shim first? | Bedrock modules (game engines need them too), libuv acceptable as their implementation |
| O5 | **Loopback token friction.** Even on localhost, IED requires the one-time token / cookie (blocks malicious web pages from driving the server). Acceptable? (The CLI and tray open URLs with the token, so normal use is seamless.) | Yes |
| O6 | **Default autonomy.** New chats default to `ask` permission mode and interrupted Agent runs auto-resume after a server restart. OK? | Yes; changed 2026-10-23: new chats default to `auto` (Plan mode keeps `plan`), `docs/11` |
| O7 | **Server platform scope.** v1 server is Windows x64 only (C4 is Windows-only today); a Linux server comes after C4's Linux target. Clients are any browser. OK? | Yes |
| O8 | **Not core**: ComfyUI generation, Perforce/Fossil, GitHub PR integration as optional plugins rather than built-ins. OK? | Yes |
| O9 | **"Explosion theming"**: purely functional grayscale, or a signature accent moment (logo, title treatment, splash)? | Functional; identity in iconography only |
| O10 | **Phone editing**: read + light edits by default (tap to edit), or full editing parity? | Read + light edit |

## Open - design (resolve before implementing the affected area)

| # | Question | Notes |
|---|---|---|
| D1 | Reflection-based JSON serialization in Bedrock (needs String/Array/HashMap/enum support) vs hand-written DTO codecs in IED | Depends on Bedrock timeline (`docs/17` checklist item 6) |
| D2 | Binary WS frames for terminal/profiler bulk | Measure in M1/M9 before adding |
| D3 | Embeddings for skills/history search: which local embedding model/endpoint by default | P2; BM25 first |
| D5 | Spell-check dictionary source and license (SCOWL-based Hunspell en_US) | Check before bundling |
| D6 | Whether Nuke should read `.iedproj` directly someday | No for v1 (Nuke stays authoritative; `nuke init --ied` generates IED files) |

## Later (v2+ candidates)

- Out-of-process isolation for third-party plugin logic (plugin C4 code runs in the server; the boundary is trust, `docs/26` section 2).
- Linux/macOS IED server builds (after C4 targets them).
- Light theme (plan says no).
- Plugin marketplace / sharing.
- Multi-user servers (v1 is single-owner).
