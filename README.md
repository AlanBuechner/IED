# IED

IED is an IDE / AI chat tool that mimics **Visual Studio** (not VS Code) with first-class local AI features. It is part of the "explosion" project family:

| Project | What it is | Where |
|---|---|---|
| **C4** | The programming language (compiler, LSP, DAP, runtime, Bedrock default library) | `C:\GameDev\C4` |
| **Nuke** | The C4 build system (Python kit: `nuke.bat`, `Config.py`, project manager, incremental builder) | `C:\GameDev\C4\Nuke` |
| **Claymore** | The game engine | `C:\GameDev\Claymore` |
| **Echo** | The old IDE (being replaced by IED; C4Assist is currently written for it in Go) | — |
| **IED** | This repo. Native C4 backend + web (html/css/js) frontend, plugin architecture | here |

## Status

**Preliminary scaffold — docs and folder structure only, no code yet.** The C4 compiler is not ready to build this repo; implementation starts once it is. Everything in `docs/` is high-level planning intended to give the implementing AI a head start, not a final spec.

## Reading order

1. [`IDEplan.md`](IDEplan.md) — the original vision (source of truth for *what* and *why*).
2. [`AGENTS.md`](AGENTS.md) — working agreements for anyone (human or AI) implementing IED.
3. [`docs/README.md`](docs/README.md) — index of the planning docs.
4. [`WORK.md`](WORK.md) — current state and next actions (living document).

## Repository map

```
ied/
├── IDEplan.md            # Original vision document
├── AGENTS.md             # Working agreements / conventions for implementers
├── WORK.md               # Living state: what exists, what's next
├── docs/                 # High-level planning docs (numbered)
├── Nuke/                 # Build system kit — ported from C:\GameDev\C4\Nuke when the compiler is ready
├── src/                  # Native host application (C4 sources later): plugin host, IPC, LSP/DAP, AI runtime
├── frontend/             # Web UI shell (html/css/js): layout, theme tokens, panels, Monaco integration
├── shared/               # Code shared between host and plugins
│   ├── host-api/         # Plugin host API definitions (manifest types, contributions, bindings)
│   └── ui-kit/           # Shared web components: tree view, tab strip, ANSI console renderer, icons
├── plugins/              # Default/bundled plugins, one folder each (each is itself a project)
│   ├── chat/             # AI chat: tabs, streaming, tool calls, model groups, context panel
│   ├── editor/           # Monaco text editor, LSP/DAP, project tree, toolbar, output window
│   ├── git/              # Fork/GitKraken-style source control UI
│   ├── terminal/         # Always-present collapsible bottom shell (PowerShell/bash)
│   ├── sandbox/          # Docker-based cross-OS build/test/develop sandbox
│   └── profiler/         # Tracy-like profiler: threads, task-grouped view, GPU traces
└── .ied/                 # IED's own solution hidden folder (mirrors C4's .echo/): settings + skills, committed
```

## Key conventions (summary — details in docs/)

- **No language/engine specifics in the host.** C4 and Claymore get their support through plugins, LSP and DAP. The host is generic.
- **Projects mirror Visual Studio:** per-project config file + a solution file that groups them. Project files may define a root folder other than their own location so generated project files can live in a build directory.
- **Hidden folder at the solution root** (`.ied/`) holds project settings and skills, so deleting generated files never loses them.
- **Builds are driven by Nuke**, not reimplemented: IED invokes `nuke.bat` with toolbar dropdown values injected into command templates.
- **Theme:** dark, grayscale base; color only where it carries meaning (saved = green, unsaved = yellow, error = red).
