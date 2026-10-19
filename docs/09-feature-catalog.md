# 09 - Feature catalog (index)

## Purpose

The full feature set of IED, one area per file under `docs/features/`. This is the "what" list the implementing AI works from: every feature has an ID, a priority, the user stories it serves (`docs/08`), and an owner (which host module, plugin, or external repo builds it). Design detail lives in the numbered docs; the catalog links to them instead of repeating them.

## How to read a catalog entry

```
| ID       | Feature                              | Pri | Stories        | Owner          |
| F-EDT-07 | Bound split (header <-> source pair) | P1  | US-EDT-6       | plugins/editor |
```

- **ID** `F-<AREA>-<n>`: stable once assigned. Never renumber; retire with a strike-through and a note.
- **Pri:** `P0` MVP daily driver, `P1` v1 complete, `P2` later (same scale as `docs/08`).
- **Owner:** `src/<module>` (host), `frontend/` (web shell), `plugins/<name>` (default plugin), `shared/ui-kit`, or `C4 repo` / `Claymore repo` for cross-repo work. A feature with two owners lists the one that is accountable first.
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

AI tool calls are catalogued separately in `docs/13-ai-tool-catalog.md` (IDs `T-<group>-<n>`), because their schemas matter more than their UI.

## Priority totals and the MVP line

The **P0 set** is defined as: the smallest IED the owner can switch to from Echo for daily C4/Claymore work. It must therefore cover Echo's parity list (`docs/20`) for the features the owner actually uses, plus the plan's non-negotiables: split server/web with resume, VS-style editor with LSP, toolbar build/run with output routing, chat with streaming + tool calls + persistence + stall protection, terminal, git basics, settings with model groups.

Everything else the plan asks for is P1. P2 is extrapolation the plan does not require.

The roadmap (`docs/18`) sequences P0 into milestones M1-M6 and P1 into M7-M10.
