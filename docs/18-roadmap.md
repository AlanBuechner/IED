# 18 - Roadmap

## Purpose

Sequence the feature catalog (`docs/09`) into milestones with clear entry conditions, scope and exit criteria, and separate **what can start now** (no C4 compiler needed) from what waits for the compiler. Each milestone ends with its journeys (`docs/08`) passing per `docs/19`, a commit, and `WORK.md` updated.

## Dependency picture

```
                         C4 repo track (docs/17)
  P-1 Bedrock Net/Process/FS.Watch/Time/Text --+
  P-2 CBind suspected bugs S1-S7 --------------+--> [C4 READY] --> M1 --> M2 --> M3 --> M4 --> M5 --> M6 (P0 / daily driver)
  P-3 scale test + thread/exception policy ----+                                                    |
                                                                                                    +--> M7 orchestration
  M0 (now): contracts, mocks, UI, harness tests ---------------------------> feeds M1-M6           +--> M8 VS depth
  P-4 Nuke N1-N6 + c4 plugin (declarative) ---> needed by M3 (C4 experience)                        +--> M9 profiler  <-- P-6 Bedrock Profile::
  P-5 C4LSP L1-L5, C4DAP depth ---------------> quality in M2/M3/M8                                 +--> M10 web, MCP, Claude, sandbox  <-- P-7 Linux (sandbox for C4)
                                                                                                    +--> M11 ecosystem + distribution
                                                                                                    +--> M12 GPU debugger (RenderDoc) --> M13 Flashbang  <-- P-8 Bedrock Gpu::
```

## M0 - Pre-compiler groundwork (can start now)

Nothing here needs the C4 compiler. It de-risks the most expensive-to-change decisions and builds everything in the browser and the contract.

| Work | Output | Features |
|---|---|---|
| Review and freeze contracts | `shared/host-api` v1 schemas reviewed by the owner; open questions in `docs/99` answered | - |
| Theme tokens | `frontend/css/theme.css` (grayscale + semantic + syntax tokens), `docs/07` values | F-SHL-50 |
| Static mocks of the six review screens (`docs/15`) | HTML/CSS against `theme.css`; screenshots at 3 viewports; owner sign-off on the look | - |
| Mock server + conformance suite | `tools/mock-server/`, `tools/conformance/` | F-SRV-20..25 (client side) |
| Frontend shell on the mock server | sidebar, docking, menus, command registry + keybindings, status bar, WS client with resume, phone shell | F-SHL-* (P0) |
| Fake model server + harness test scripts | `tools/fake-model/` and scripted cases for every stop class | `docs/12` |
| SearXNG-on-Windows spike | yes/no + pinned commit + startup recipe | `docs/21` |
| Echo importer design check | sample conversions of the C4 repo's `.echo/` | `docs/20` |

**C4-repo work that can run in parallel now:** Nuke N1-N6 and the declarative `c4` plugin manifest (validates against the schema today), C4LSP L1-L5, verifying suspected bugs S1-S7.

**Exit:** owner signs off the mocks; frontend shell passes conformance-driven e2e on the mock server at 3 viewports, including the resume scenarios; harness scripts exist for every stop class.

## M1 - Server skeleton (entry: C4 READY)

Lifecycle + single instance + named-pipe CLI; HTTP static + auth (loopback token, owner password) + WS hub with topics, ring buffers, resume, backpressure; settings tiers + validation; plugin host for **built-in and declarative** plugins; workspace open (solution/folder/file); FS service + watching; document store with leases and rebase; logs and the IED output source; crash-safe persistence helpers.

**Exit:** the M0 frontend runs unchanged against the real server; the conformance suite passes on both servers; security tests (Host/Origin/token) pass; 24 h idle soak clean.

## M2 - Editor core

Monaco wired to the document store; Solution Explorer (groups, icons, decorations, file ops, trash); tabs (preview tab, dirty state), splits; LSP pool with C4LSP (staged launch, encoding override, refetch quirks); completion/hover/signature/definition/references/rename/format/code actions/semantic tokens/folding/symbols; Error List (LSP); quick find/replace and find in files (ripgrep); track-changes margin; indentation guides + tabs default; Go To All; navigation back/forward; header/source switch.

**Exit:** J10; open the Claymore and C4 repos and navigate/edit with C4LSP at budget latencies; screenshots at 3 viewports.

## M3 - Build, run, debug loop

Task runner (steps, variables, conditions, job objects, cancellation); toolbar from project files and presets; output window (sources dropdown, ANSI, clickable locations); problem matchers + path resolution; Error List build source; terminal dock with ConPTY, multiple instances, persistence; DAP pool + C4DAP; breakpoints, stepping, Locals/Watch/Call Stack/Threads, DataTips, exception settings; F5 / Ctrl+F5 / Ctrl+Shift+B.

Needs from C4 repo: Nuke N1, N2, N3 (streaming), N8 (Claymore `-g`), the `c4` plugin.

**Exit:** J1 end-to-end on Claymore.

## M4 - AI runtime core and chat

OpenAI-compatible provider with discovery and probing; dialect adapters; **full stall/recovery harness** (`docs/12`); agent loop (Ask/Plan/Agent modes, `finish`), permissions + approval cards, steering/inject; core tools (fs, search, shell in AI terminals, symbol-first code tools, build/problems, tasks, ask_user); model groups + scheduler + privacy classes; chat persistence (step journal, trajectory with retention); chat UI (tabs, docking, streaming segments with telemetry, tool cards, stop reasons, input bar with @-mentions and attachments).

**Exit:** J11 (all scripted stop classes recover and explain themselves), J4 against a real local model, a 2-hour unattended Agent-mode run on the owner's box with zero unexplained stops.

## M5 - Git, Mission Control, phone

Git plugin core (graph, details, local changes, stage by hunk/line, commit with AI message, fetch/pull/push, stash, branches); Mission Control run list + attention queue; notifications (in-app + browser); phone-first views (chat, approvals, diff review, build status) polished.

**Exit:** J2 (leave it running, steer from the phone), J10 at phone width.

## M6 - P0 complete: the daily driver

Context ledger + tiered compression + history tools; checkpoints + rewind; skills index/search + skill_write; settings window complete for P0 (models, groups, prompts, plugins, remote access); privacy taint; Echo importer; portable zip.

**Exit:** J9 over a real multi-day chat; the owner switches from Echo; P0 list in `docs/09` all done; `docs/20` parity floor rows covered for the features the owner uses.

## M7 - Orchestration (the big step past Echo)

Goals, queue and board; worktree isolation and the review stage; independent verifier; sub-agents (research/worker) with slot-bounded parallelism; schedules and away mode; interactive priority; scorecards; eval harness; learning-loop proposals.

**Exit:** J3, J13; eval suite running per IED build.

## M8 - Visual Studio depth

Full debugger windows (Immediate, Modules, Memory, Disassembly, conditional/hit/log/function/data breakpoints as adapters allow, run to cursor, set next statement, attach); Test Explorer + CodeLens; inline AI (Ctrl+I, AI actions, lightbulb fixes) and ghost-text FIM completion; code-aware spell check; bound splits; map-mode scrollbar; markdown preview with interactive checkboxes; browser pane + port forwarding; bookmarks, task list, navigation bar, document outline; 3-way merge editor; interactive rebase.

Needs: C4DAP depth, C4LSP L6-L7, Nuke N4-N5.

**Exit:** a VS-parity checklist walk-through (`docs/15` keymap and windows) with screenshots.

## M9 - Profiler (entry: Bedrock `Profile::`)

ITP ingest + chunked storage + indexes; timeline with frames, zones, messages, plots, thread rename tints; by-task view; statistics, find-zone, flame graph; memory (leaks, pools, GC events); GPU lanes with calibration (D3D12); capture library; Chrome-trace import; AI profiler tools.

**Exit:** J5 on a Claymore build.

## M10 - Web, MCP, Claude, sandbox

Bundled SearXNG + tor + proxy profiles and per-workspace egress policy; web_fetch/search, headless browser tools (Edge CDP), media tools (window capture, image compare, video frames); MCP client (stdio + HTTP) and IED-as-MCP-server; Anthropic API provider and the Claude Code bridge with usage windows; Docker sandbox provider with egress gateway, task targets and AI tools.

**Exit:** J6 (on a Linux-capable project; C4 projects once P-7 lands), J8, J12 (with a Claymore MCP server or plugin).

## M11 - Ecosystem and distribution

JS plugin runtime (QuickJS-ng behind a C shim) and process plugins; install/approval/update for third-party plugins; typings + plugin template + authoring skill; federations + cross-workspace handoff; installer + Explorer integration + first-run; desktop wrapper (WebView2); self-update.

**Exit:** J7, J14; a third-party-style sample plugin built only from public docs.

## M12 - GPU debugger on the RenderDoc backend

`ied.gpu` with the `rdc` backend process (C++ over `renderdoc_replay.h`, pinned RenderDoc): capture task and library, Event Browser, Pipeline State, Texture Viewer (WebGL2 display + backend overlays), Mesh Viewer, Resources, API Calls, Messages, Pixel History, Shader Debugger, per-event timing, Edit and Replay, compare, insights, AI GPU tools, phone triage. Design: `docs/22`.

Needs: process spawn and blob transport (M1), docking (M2), tasks (M3); GPU zones and capture on hitch need M9.

**Exit:** J15 on a Claymore build, with RenderDoc's current limits noted (unbounded cbuffer arrays are not shader-debuggable there).

## M13 - Flashbang (IED's own capture and replay)

Bedrock `Gpu::` recorder for C4 programs (C4 repo, P-8), `.flash` format with metadata chunk, `flashbang-replay` backend, time-travel shader debugging by instrumented replay, work graphs, rolling capture on hitch.

**Exit:** J15 fully on Flashbang, including stepping `DefaultLit` with its unbounded `u_Materials[]`, and a work-graph pass.

## Continuous throughout

- Every milestone: docs and skills updated to match behavior; `WORK.md` checkpoint; journeys re-run (no regressions in earlier ones).
- Harness and eval suites run on every server build from M4 on.
- C4 bugs found while building IED go to `C:\GameDev\C4\docs\status\known-bugs.md`, get fixed upstream, validated, and removed (AGENTS.md) - not worked around permanently in IED.
