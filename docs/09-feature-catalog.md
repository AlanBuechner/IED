# 09 - Feature catalog (index)

## Purpose

The full feature set of IED, one area per file under `docs/features/`. This is the "what" list the implementing AI works from: every feature has an ID, a priority, the user stories it serves (`docs/08`), and an owner (which host module, plugin, or external repo builds it). Design detail lives in the numbered docs; the catalog links to them instead of repeating them.

## How to read a catalog entry

```
| ID       | Feature                              | Pri | Stories        | Owner          |
| F-EDT-71 | Bound split (header <-> source pair) | P1  | US-EDT-6       | plugins/editor |
```

- **ID** `F-<AREA>-<n>`: stable once assigned. Never renumber; retire with a strike-through and a note.
- **Pri:** `P0` MVP daily driver, `P1` v1 complete, `P2` later (same scale as `docs/08`).
- **Owner:** `src/<module>` (host), `frontend/` (web shell), `plugins/<name>` (default plugin), `shared/ui-kit`, or `C4/` (the C4 tree) / an engine plugin for work outside the IED host. A feature with two owners lists the one that is accountable first.
- Entries marked **(gen)** are generic host capabilities a *language/engine plugin* builds on; IED itself ships no language-specific behavior (AGENTS.md rule 1).

## Areas

| File | Area | Code |
|---|---|---|
| [features/01-server-and-sessions](features/01-server-and-sessions.md) | Server lifecycle, multi-workspace, sessions, resume, devices, notifications, port forwarding | SRV |
| [features/02-shell-and-layout](features/02-shell-and-layout.md) | Sidebar, docking, menus, toolbar host, status bar, command system, keymap, phone layout | SHL |
| [features/03-workspaces](features/03-workspaces.md) | Solutions, projects, folders, files, federations, trust, Solution Explorer, templates | WSP |
| [features/04-editor](features/04-editor.md) | Monaco + LSP, VS editing/navigation parity, search, splits, spell check, viewers, inline AI | EDT |
| [features/05-build-run-test](features/05-build-run-test.md) | Toolbar command templates, output window, problem matchers, Error List, Test Explorer | BLD |
| [features/06-debugger](features/06-debugger.md) | DAP client and VS debug windows | DBG |
| [features/07-chat](features/07-chat.md) | Chat UI: tabs, streaming, telemetry, approvals, scratchpad, tasks, context panel, history | CHT |
| [features/08-ai-runtime](features/08-ai-runtime.md) | Providers, routing, privacy, agent loop, reliability, context, skills, MCP, egress, Claude | AIR |
| [features/09-git](features/09-git.md) | Fork/GitKraken-class source control | GIT |
| [features/10-terminal](features/10-terminal.md) | Bottom terminal, PTY supervisor, AI terminals | TRM |
| [features/11-sandbox](features/11-sandbox.md) | Docker/WSL/SSH sandboxes, egress, desktop view | SBX |
| [features/12-profiler](features/12-profiler.md) | Profiler UI, capture, memory, GPU, by-task view, AI queries | PRF |
| [features/13-plugins](features/13-plugins.md) | Plugin host, contribution points, bindings, trust, plugin dev loop | PLG |
| [features/14-settings](features/14-settings.md) | Settings window, tiers, secrets, keybindings, model config UI | SET |
| [features/15-ops-and-distribution](features/15-ops-and-distribution.md) | Logs, health, crash recovery, trash, installer, CLI, bundled services, updates | OPS |
| [features/16-orchestration](features/16-orchestration.md) | Mission Control, goal queue/board, worktree-isolated parallel agents, verifier, schedules, cross-workspace handoff, model scorecards and evals | ORC |
| [features/17-gpu-debugger](features/17-gpu-debugger.md) | GPU debugger: capture library, event browser, pipeline state, texture/mesh viewers, pixel history, shader debugging, performance, compare, swappable replay backends (RenderDoc, Flashbang) | GPU |

AI tool calls are catalogued separately in `docs/13-ai-tool-catalog.md` (IDs `T-<group>-<n>`), because their schemas matter more than their UI.

## Ambition: surpass the predecessor, do not port it

Owner directive (2026-10-19): **IED should not just have parity with the predecessor IDE but drastically surpass it.** Predecessor parity (`docs/20`) is the *floor* - a checklist of things IED must not regress - never the definition of done. Where IED is designed to be in a different league:

| Area | Predecessor (today) | IED |
|---|---|---|
| Harness reliability | Stops when a reply has no tool call (outside Goal mode); text tool calls unparsed; mid-stream errors fatal; wrong context sizes | Stop taxonomy, bounded recovery for every class, dialect normalization, real `n_ctx` discovery, visible "model or harness" answer (`docs/12`) |
| Orchestration | One model per chat + read-only research helpers; Kanban/heartbeat designs never built | Mission Control, goal queue + board, parallel agents in worktrees, independent verifier, schedules, away mode, cross-workspace handoff (ORC) |
| Model use | Per-chat endpoint, vision rerouting | Role-based model groups with temperatures, slot-aware scheduler, limit/load/capability/privacy routing, scorecards, eval arena (AIR, ORC) |
| Privacy | none | Privacy classes, local-only policy, sticky taint tracking, tor/VPN egress (AIR) |
| IDE | VS Code-like code view | Visual Studio-class: Error List, problem matchers, Test Explorer, full debugger windows, toolbar task templates, bound splits, map-mode scrollbar, code-aware spell check (EDT, BLD, DBG) |
| State | Server-owned chats; unsaved buffers in browser IndexedDB | Everything server-owned incl. unsaved buffers, layouts per device class, seq-numbered resume (SRV, `docs/10`) |
| Context | One summarizer pass, chars/3.5 estimates | Ledger, tiered + cache-aware compression, real tokenizers, history retrieval, scratchpad and tasks never compressed away (AIR) |
| Tools | Generic file/shell/web | Symbol-first LSP tools for small-context local models, debugger/build/test/profiler/browser/window-capture/image-diff tools, on-demand tool groups (`docs/13`) |
| Extensibility | iframe views + JSON-RPC tools | Composable bindings, declarative language plugins, project-local plugins, 40+ contribution points (PLG) |
| Game dev | none | Profiler with by-task view and GPU lanes, a PIX/RenderDoc-class GPU debugger with AI and capture compare, engine plugin hooks, visual verification (PRF, GPU, `docs/13`) |
| Cloud | none | Claude via the user's own Claude Code CLI or API key, usage windows and reset times (AIR) |

Several items the first draft marked P2 were raised to P1 because they are what makes the difference (ghost-text completion, IED as an MCP server, worktree isolation, project memory).

## Priority totals and the MVP line

The **P0 set** is the first release the owner switches to **and immediately feels the upgrade from the predecessor IDE*: it covers the predecessor parity list (`docs/20`) for the features the owner actually uses, the plan's non-negotiables (split server/web with resume, VS-style editor with LSP, toolbar build/run with output routing, chat with streaming + tool calls + persistence, terminal, git basics, settings with model groups), **and** the first differentiators: the full stall-protection harness, symbol-first LSP tools, Mission Control's run list + attention queue, server-owned unsaved buffers, and privacy classes.

Everything else the plan asks for, plus the orchestration layer, is P1. P2 is extrapolation the plan does not require.

The roadmap (`docs/18`) sequences P0 into milestones M1-M6 and P1 into M7-M11.
