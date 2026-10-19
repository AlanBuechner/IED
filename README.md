# IED

IED is an IDE / AI workbench that mimics **Visual Studio** (not VS Code) with first-class **local AI** - built for the "explosion" project family, and designed to drastically surpass Echo, the IDE it replaces:

| Project | What it is | Where |
|---|---|---|
| **C4** | The programming language (compiler, LSP, DAP, runtime, Bedrock default library) | `C:\GameDev\C4` |
| **Nuke** | The C4 build system (Python kit: `nuke.bat`, `Config.py`, `Project.py` discovery, incremental builder) | `C:\GameDev\C4\Nuke` |
| **Claymore** | The game engine (C4 port of IcepickRPG) | `C:\GameDev\Claymore` |
| **Echo** | The old IDE (Go + TypeScript), being replaced; parity is the floor, not the goal | `C:\GameDev\echo` |
| **IED** | This repo. **Split server + web client:** a C4 server owns all state (plugins, LSP/DAP, AI runtime, builds, terminals, agents); the UI is a web app for any browser - desktop or phone - that you can close while the server keeps working | here |

## Status

**Planning complete for the v1 scope; no product code yet.** The C4 compiler/library cannot build the server yet - `docs/17` lists exactly what is missing. Work that does not need the compiler (contracts, frontend on a mock server, test doubles) is milestone M0 in `docs/18`.

## Reading order

1. [`IDEplan.md`](IDEplan.md) - the original vision (source of truth for *what* and *why*).
2. [`AGENTS.md`](AGENTS.md) - working agreements for anyone (human or AI) implementing IED.
3. [`docs/README.md`](docs/README.md) - index and reading order of the planning docs (stories -> feature catalog -> roadmap -> design docs).
4. [`WORK.md`](WORK.md) - current state and next actions.

## Repository map

```
ied/
+- IDEplan.md            vision (owner)
+- AGENTS.md             working agreements and hard rules
+- WORK.md               living state: what exists, what is next, decisions log
+- docs/                 numbered planning docs + features/ (the full feature catalog)
+- shared/
|  +- host-api/          THE CONTRACT: JSON Schemas (plugin manifest, solution, project, federation, agent mode),
|  |                     WS frame schema, process-plugin RPC, skill format, examples (incl. the C4 plugin)
|  +- ui-kit/            shared web components (tree, tabs, ANSI console, menus, dropdowns, icons)
+- src/                  IED server (C4, when the compiler is ready); src/ai/prompts/ already holds the shipped prompt texts
+- frontend/             web client (html/css/js): shell, docking, theme tokens, WS client
+- plugins/              built-in plugins: chat, editor, git, terminal, sandbox, profiler
+- Nuke/                 build kit (ported from the C4 repo when the compiler can build IED)
+- tools/                repo tooling: fetch-deps.ps1 (+ planned: mock-server, fake-model, conformance)
+- third_party/          vendored dependencies (gitignored; tools/fetch-deps.ps1 reproduces it)
+- .ied/                 IED's own committed solution folder (skills); .ied/local/ is gitignored
```

## Key conventions (details in `docs/`)

- **Split server + web client; the server owns all state** (even unsaved buffers). Close the browser, switch to the phone, reconnect - everything resumes.
- **No language/engine specifics in the host.** C4 and Claymore get support through plugins (often purely declarative), LSP, DAP and MCP.
- **Visual Studio parity** for the IDE (menus, keymap, Error List, Output dropdown, debugger windows, toolbar tasks with dropdown-injected `${variables}`).
- **AI that works unattended**: role-based model groups, slot-aware routing with privacy guarantees, a harness that never "stops randomly" without saying why, Mission Control for many parallel agents, verified results.
- **Builds are driven by Nuke** (or any build system a plugin configures) - never reimplemented.
- **`.ied/` at the solution root is committed** (settings, skills, modes); `.ied/local/` is per machine.
- **Theme:** dark, grayscale base; color only where it carries meaning; tokens only.
