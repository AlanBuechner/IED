---
name: ied-project
description: How the IED repository is organized and how to work on it — read this before implementing anything in IED.
keywords: [ied, ide, repo layout, docs, workflow, nuke, plugins]
---

# Working on IED

IED is a Visual-Studio-style IDE with first-class local AI, built in C4 (native host) + web frontend (html/css/js), plugin architecture. It is part of the explosion family: C4 (language, `C:\GameDev\C4`), Nuke (build system, `C:\GameDev\C4\Nuke`), Claymore (engine, `C:\GameDev\Claymore`).

## Read first, in order

1. `IDEplan.md` — the vision; source of truth for intent.
2. `AGENTS.md` — working agreements and hard rules (generic host only, Nuke drives builds, `.ied/` committed, theme discipline).
3. `WORK.md` — current state + next actions; **update it at every checkpoint**.
4. `docs/` — numbered planning docs; `docs/99-open-questions.md` lists blocking decisions (webview, IPC v1, manifest v1, project/solution schemas) that must be resolved before implementation starts.

## Repo map

- `src/` — native host (C4): shell, IPC, settings, plugin host, LSP/DAP pools, AI runtime, terminal/git/docker supervisors. No language/engine specifics allowed here.
- `frontend/` — webview UI: layout engine, theme tokens (`css/theme.css` is the single visual source of truth), Monaco integration.
- `shared/host-api/` — the versioned contract (IPC protocol, manifest schema, project/solution schemas). Freeze before plugins exist.
- `shared/ui-kit/` — shared web components (tree, tabs, ANSI console, icons, context menus).
- `plugins/` — default plugins: chat, editor, git, terminal, sandbox, profiler. Each is a normal Nuke-built project with a manifest.
- `Nuke/` — build kit ported from `C:\GameDev\C4\Nuke`; IED never reimplements builds, it invokes `nuke.bat`.
- `.ied/` — committed solution hidden folder (settings + skills); `.ied/local/` is per-machine and gitignored.

## Ground rules that bite if forgotten

- C4/Claymore support arrives **only** via plugins/LSP/DAP — never special-case them in host code.
- Toolbar buttons are command templates with `{var}` injection from dropdowns; steps route output to named tabs and can chain on success (`onlyIf: prevSuccess`).
- LSP init options are build-derived (exact compiler includes + defines) so diagnostics match real builds — same pattern as Nuke's current `.echo/c4-lsp.json` bridge.
- Theme: dark, grayscale base, color only for meaning (green=saved, yellow=unsaved, red=error, blue=info). Plugins use tokens, never raw hex.
- Process loop: design → implement → test → review → repeat; commit at checkpoints; screenshot-verify UI interactions.
