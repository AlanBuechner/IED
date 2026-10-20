# 99 - Open questions and decision record

Decide open items during design, then **move the answer into the relevant doc and record it here** (one line, so history stays visible). Items marked *owner* are genuinely the owner's call; the rest have a recommended answer the implementer may adopt unless the owner objects.

## Resolved

| # | Question | Decision | Where |
|---|---|---|---|
| 1 | UI delivery model | Split server + served web client; WebView2 desktop wrapper is optional later (P2) | `docs/01`, `docs/21` |
| 2 | Server <-> client protocol v1 | One WS per tab; req/res + topic events with seq + snapshots + gaps; ring-buffer resume; backpressure; idempotent `opId`; doc lease + rebase | `docs/10`, `shared/host-api/protocol/` |
| 3 | Plugin manifest v1 | Drafted as JSON Schema with four runtimes (builtin, declarative, js, process) and 40+ contribution points; frozen after owner review | `docs/02`, `shared/host-api/schemas/plugin-manifest.v1.schema.json` |
| 4 | Project/solution schemas + toolbar grammar | Drafted as JSON Schema; variables use `${name}`; tasks = steps (run/task/debug/binding) with `onlyIf`/`when`, output targets, matchers; federations added | `docs/03`, `docs/06`, `shared/host-api/schemas/` |
| 5 | C4 compiler readiness | Gap analysis done; readiness checklist of 10 items; C4-tree work packages P-1..P-7 | `docs/17` |
| 6 | JS plugin runtime | QuickJS-ng kept for in-process JS logic (behind a C shim because of CBind union issues), plus out-of-process plugins over `ied-plugin-rpc/1` for anything heavy or native; built-ins compiled into the server because C4 has no DLL output | `docs/02`, `docs/17` |
| 7 | Context compression details | Ledger + tiered compression (mechanical first, then `summarizer` role); cache-aware (append-only between large compressions); 70% warn / 85% compress defaults; history tools | `docs/04`, `docs/12` |
| 8 | SearXNG packaging | Bundled with embeddable Python + waitress (spike in M0 to confirm on Windows); fallback isolated auto-install (WSL/Docker); tor bundled | `docs/21` |
| 9 | Profiler trace format | ITP v1: chunked varint event stream, explicit string interning, timestamped thread renames, task events, GPU calibration, streamed to `.iedtrace` | `docs/16` |
| 10 | Legacy workspace migration | The predecessor IDE's workspace folder stays in place; IED never writes it; Nuke keeps writing it for now but nothing depends on the predecessor; `ied import legacy` converts settings/skills/modes/chats | `docs/17` (N9), `docs/20` |
| 11 | Claude integration shape | Two providers: Anthropic API (API key) and a bridge that drives the owner's own installed, self-signed-in `claude` CLI in stream-json mode with IED tools via MCP; IED never touches Claude credentials (Anthropic policy) | `docs/04` |
| 12 | Request routing policy | Role fallback lists; failure-class triggers; slot-aware scheduler with interactive priority; privacy always wins (queue rather than leak); capability matching; scorecards optional | `docs/04`, `docs/features/08`, `docs/features/16` |
| 13 | Remote access security and concurrency | Loopback default; Host/Origin checks; loopback one-time token; owner password required for LAN; device sessions; TLS via Tailscale (built-in TLS P2); explicit per-resource concurrency rules | `docs/11` |
| 14 | Git backend | git CLI for everything (porcelain v2 / NUL-separated formats); libgit2 stays vendored for a possible graph optimization later | `docs/features/09-git.md` |
| 15 | Ambition level | Owner: not just parity with the predecessor IDE but drastically surpass it - predecessor parity is the floor; orchestration area added; differentiators raised to P1 | `docs/09`, `docs/20`, `docs/features/16` |
| 16 | Syntax highlighting colors (was O4) | Owner: use the Visual Assist X scheme for code. Default palette = the old C4 editor plugin's `c4-visual-assist-dark` values; a Grayscale palette stays as a per-user option | `docs/07`, `frontend/css/theme.css` |
| 17 | GPU debugger look | Owner: PIX's layout and styling, not RenderDoc's - capture as a document with view tabs, timing timeline on top, events grid, Pipeline state tree beside a resource viewer, docked grids; RenderDoc stays a backend and a capability reference | `docs/22`, `frontend/mocks/07-09` |
| 18 | Where C4 and Nuke live | Owner: in this repository (`C4/`, `Nuke/`), versioned together with IED so an agent can fix the compiler or the IDE and continue; Frost folded in; old history dropped | `README.md`, `AGENTS.md` |
| 19 | Nuke's project format and extensibility | Owner: declarative TOML `nuke.toml` plus optional Python (`scripts`, `python =` steps); every command has pre-/post- phases for project steps; workspaces add commands; commands chain (`nuke build run dev`) | `docs/23`, `Nuke/README.md` |
| 20 | How projects get the compiler | One shared store of immutable, versioned C4 toolchains in this checkout (`toolchains/c4/<version>`, plus `dev` = the in-tree build); workspaces pin a version; nothing vendors Nuke or C4 | `docs/23` N-7 |

## Open - owner's call

| # | Question | Recommendation |
|---|---|---|
| O1 | **"Federated projects"** - is it a set of solutions/repos opened together as one workspace (e.g. this repo with IED + C4, plus a game project), as designed in `docs/03`? Or something else (e.g. a remote project on another machine)? | As designed |
| O2 | **Claude subscription use.** Anthropic's policy forbids third-party apps from offering claude.ai login or routing requests through Pro/Max credentials; an end user may run the official `claude` CLI signed in themselves. IED's bridge drives *your own* installed CLI for personal use and never reads credentials. If IED is ever distributed to others, should the bridge be off by default (API key only) unless Anthropic approves? | Yes: bridge = personal-use feature, documented; API key path for everyone else |
| O3 | **OS primitives for the server**: build them as Bedrock modules (`Net::`, `Process::`, `FS::Watch`, `Time::`) in the C4 tree (`C4/Bedrock/`), or wrap libuv behind a C shim first? | Bedrock modules (game engines need them too), libuv acceptable as their implementation |
| O5 | **Loopback token friction.** Even on localhost, IED requires the one-time token / cookie (blocks malicious web pages from driving the server). Acceptable? (The CLI and tray open URLs with the token, so normal use is seamless.) | Yes |
| O6 | **Default autonomy.** New chats default to `ask` permission mode and interrupted Agent runs auto-resume after a server restart. OK? | Yes |
| O7 | **Server platform scope.** v1 server is Windows x64 only (C4 is Windows-only today); a Linux server comes after C4's Linux target. Clients are any browser. OK? | Yes |
| O8 | **Not core**: ComfyUI generation, Perforce/Fossil, GitHub PR integration as optional plugins rather than built-ins. OK? | Yes |
| O9 | **"Explosion theming"**: purely functional grayscale, or a signature accent moment (logo, title treatment, splash)? | Functional; identity in iconography only |
| O10 | **Phone editing**: read + light edits by default (tap to edit), or full editing parity? | Read + light edit |
| O11 | **GPU debugger backend** (`docs/22`): RenderDoc as the replay backend, our own ("Flashbang", RenderDoc as reference), or both? Neither needs Python (RenderDoc's replay API is C++). Also: ship RenderDoc's DLL inside IED, and is "Flashbang" the name? | Both behind one protocol: RenderDoc backend first (complete debugger fast), Flashbang for C4 programs where control matters (bindless/unbounded arrays, work graphs, engine metadata, time-travel debugging); bundle a pinned RenderDoc (MIT); name Flashbang |

## Open - design (resolve before implementing the affected area)

| # | Question | Notes |
|---|---|---|
| D1 | Reflection-based JSON serialization in Bedrock (needs String/Array/HashMap/enum support) vs hand-written DTO codecs in IED | Depends on Bedrock timeline (`docs/17` checklist item 6) |
| D2 | Binary WS frames for terminal/profiler bulk | Measure in M1/M9 before adding |
| D3 | Embeddings for skills/history search: which local embedding model/endpoint by default | P2; BM25 first |
| D4 | Exact prefill-progress signals available from llama.cpp/vLLM/Ollama streams | Verify field names during M4; the estimate-based watchdog works without them |
| D5 | Spell-check dictionary source and license (SCOWL-based Hunspell en_US) | Check before bundling |
| D6 | Whether Nuke should read `.ied.proj` directly someday | No for v1 (Nuke stays authoritative; `nuke init --ied` generates IED files) |

## Later (v2+ candidates)

- Out-of-process isolation for *all* third-party plugin logic (v1: JS in-process with permissions, process plugins isolated).
- Linux/macOS IED server builds (after C4 targets them).
- Light theme (plan says no).
- Plugin marketplace / sharing.
- Multi-user servers (v1 is single-owner).
