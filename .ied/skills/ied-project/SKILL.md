---
name: ied-project
description: How the IED repository is organized and how to work on it - read this before planning or implementing anything in IED.
keywords: [ied, ide, repo layout, docs, workflow, nuke, plugins, roadmap, feature catalog, schemas]
---

# Working on IED

IED is a Visual Studio-style IDE with first-class local AI, built as a C4 server that owns all state plus a web client usable from any browser (including phones). It replaces Echo and must drastically surpass it. Family: C4 (`C:\GameDev\C4`), Nuke (C4 build system), Claymore (`C:\GameDev\Claymore`), Echo (`C:\GameDev\echo`, the old IDE).

## Read first, in order

1. `IDEplan.md` - the vision; source of truth for intent.
2. `AGENTS.md` - working agreements and hard rules.
3. `WORK.md` - current state, next actions, decisions log; update it at every checkpoint.
4. `docs/README.md` - reading order: `08` user stories -> `09` + `features/` feature catalog -> `18` roadmap -> design docs -> `99` decisions/open questions.

## Where the answers are

- Contracts (frozen formats): `shared/host-api/` - manifest, solution, project, federation, agent-mode and WS frame schemas, process-plugin RPC, skill format, validated examples.
- Protocol: `docs/10`. Security and multi-device rules: `docs/11`. Agent loop and stall protection: `docs/12`. AI tools: `docs/13`. Prompts: `docs/14` + `src/ai/prompts/`. UI: `docs/15`. Profiler + ITP: `docs/16`.
- What C4 must provide first, suspected C4 bugs, and C4LSP/C4DAP/Nuke/C4Assist/Bedrock work items: `docs/17`.
- Testing (mock server, fake model server, resume tests, screenshots at 3 viewports): `docs/19`.

## Ground rules that bite if forgotten

- C4/Claymore support arrives **only** via plugins (mostly declarative), LSP, DAP and MCP - never special-case them in host code.
- The server owns all state, including unsaved buffers and layouts; no feature may depend on a browser tab staying open.
- Tasks are step lists with `${variables}` from toolbar dropdowns, output targets, problem matchers and `onlyIf` conditions; Nuke stays the build authority.
- LSP init options are build-derived (`.ied/local/lsp-options/`), same pattern as Nuke's `.echo/c4-lsp.json` bridge.
- Native APIs go through hand-written C shims with opaque handles until the suspected CBind struct bugs are verified and fixed.
- A C4 compiler/library bug found while working on IED goes into `C:\GameDev\C4\docs\status\known-bugs.md`, gets fixed and validated upstream, then removed - no permanent workarounds here.
- Theme: dark, grayscale; color only for meaning; tokens only, never raw hex.
- Process: design -> implement -> test -> review; commit at checkpoints; screenshot-verify interactions at desktop and phone widths; verify the resume path.
