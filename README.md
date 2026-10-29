# IED

IED is an IDE / AI workbench that mimics **Visual Studio** (not VS Code) with first-class **local AI**. It is a
**split server + web client**: a C4 server owns all state (plugins, LSP/DAP, AI runtime, builds, terminals,
agents) and the UI is a web app for any browser - desktop or phone - that you can close while the server
keeps working.

This repository holds everything IED is built from:

| Part | What it is | Where |
|---|---|---|
| **IED** | The IDE: server (`src/`), web client (`frontend/`), built-in plugins, the host API contracts | `src/`, `frontend/`, `plugins/`, `shared/` |
| **C4** | The programming language IED is written in: compiler, LSP, DAP, runtime, the Bedrock default library, tests and the language docs | [`C4/`](C4/) |
| **Nuke** | The build system for C4 workspaces, shared by every project on the machine, with versioned C4 toolchains | [`Nuke/`](Nuke/README.md) |

Because the language lives here, an agent that finds a compiler or IDE bug while working fixes it at the
source, rebuilds, and carries on (see "When the compiler is wrong" in [`Nuke/README.md`](Nuke/README.md#8-toolchains)).

## Status

**Planning complete for the v1 scope; M0 in progress** (contracts, static UI mocks, mock server, conformance
suite, fake model server). The C4 compiler cannot build the IED server yet - `docs/17` lists what is missing;
the roadmap is `docs/18`.

## Install in one command (Windows x64)

In PowerShell, on any Windows 10/11 x64 machine - nothing needs to be installed first, not even git or Visual Studio:

```powershell
irm https://raw.githubusercontent.com/AlanBuechner/IED/main/install.ps1 | iex
```

[`install.ps1`](install.ps1) gets git when the machine has none (portable MinGit, in `%LOCALAPPDATA%\IED\git`), clones
into `%USERPROFILE%\IED` (set `IED_DIR` to change it), runs `setup.bat` - which downloads a private Python, LLVM, and
the MSVC C/C++ runtime and Windows SDK straight from Microsoft's Visual Studio packages (Microsoft's license terms apply;
setup prints the link) - builds the C4 compiler and IED, and installs IED. About 6 GB of disk; re-running it updates and
continues.

## Getting started (Windows x64)

Prerequisites: git. `setup.bat` downloads everything else, pinned by SHA256 in `tools/setup/deps.toml` - including the
MSVC runtime and Windows SDK every C++ and C4 build compiles and links against (`vendor/msvc`), so no Visual Studio
is needed and every machine builds alike.

```bat
git clone https://github.com/AlanBuechner/IED.git
cd IED
setup.bat                 :: private Python, git submodules, LLVM 22.1.0, MSVC runtime + Windows SDK, web libraries (nothing large is committed)
nuke build                :: build the C4 compiler and tools; installs the "dev" C4 toolchain
nuke test                 :: the compiler's unit tests
nuke gates                :: every C4 test suite
```

`setup.bat` offers to put `bin/` on your PATH so `nuke` works in any folder (or pass `--add-to-path`;
without it, use `nuke.bat` here). `setup.bat --toolchain` does the setup and the first build in one go.

IED itself, as an app in your Start menu (Windows key, "ied") and `ied` on your PATH:

```bat
nuke install-ied          :: build the server, the CLI and the desktop app; install into %LOCALAPPDATA%\Programs\IED
ied C:\game               :: in a new terminal: open a folder in IED (or File > Open Folder in its window)
```

A new C4 program anywhere on the machine:

```bat
mkdir MyGame && cd MyGame
nuke init                 :: nuke.toml, src/main.cf, .gitignore, AI skills in .ied/skills
nuke build run            :: compiles with the pinned toolchain from this repository's store
```

## Reading order

1. [`IDEplan.md`](IDEplan.md) - the original vision (source of truth for *what* and *why*).
2. [`AGENTS.md`](AGENTS.md) - working agreements for anyone (human or AI) working here.
3. [`docs/README.md`](docs/README.md) - index and reading order of the planning docs (stories -> feature catalog -> roadmap -> design docs).
4. [`WORK.md`](WORK.md) - current state and next actions.
5. For the language: [`C4/docs/README.md`](C4/docs/README.md), [`C4/AGENTS.md`](C4/AGENTS.md) and [`C4/WORK.md`](C4/WORK.md).

## Repository map

```
IED/
+- IDEplan.md            vision (owner)
+- AGENTS.md             working agreements and hard rules
+- WORK.md               living state: what exists, what is next, decisions log
+- setup.bat             installs what is not committed (tools/setup/deps.toml pins it)
+- nuke.toml             this repository as a Nuke workspace (the C4 compiler, the IED server, the CLI, the desktop app)
+- nuke.bat, bin/        the nuke launcher (bin/ goes on PATH)
+- cli/, launcher/       `ied`, the command line, and what it shares with the desktop app (the control pipe, starting the server)
+- desktop/              `ied-desktop`, the desktop app: the web client in WebView2 windows, written in C4 (docs/27)
+- docs/                 numbered planning docs + features/ (the full feature catalog)
+- shared/
|  +- host-api/          THE CONTRACT: JSON Schemas (plugin manifest, solution, project, federation, agent mode),
|  |                     WS frame schema, skill format, examples (a solution, a project, a plugin with C4 code)
|  +- ui-kit/            shared web components (tree, tabs, ANSI console, menus, dropdowns, icons)
+- src/                  IED server (C4): the host's concepts (workspaces, documents, tasks, language servers, ...) and the plugin API (src/api/, docs/26)
+- frontend/             web client (html/css/js): theme tokens, static mocks, shell, docking, WS client
+- plugins/              built-in plugins: one per language (c4 - with its Nuke package and the C4Api library for tools -, cpp - with Nuke's C/C++ driver and the C++ libraries -, rust, go, python, web, ...) and the features (chat, editor, git, terminal, browser, markdown, nuke, profiler, gpu, sandbox)
+- C4/                   the C4 language (compiler sources, Bedrock, runtime, LSP, DAP, tests, docs)
+- Nuke/                 the build system (Python, stdlib only) + its tests
+- tests/                e2e: the web client in Playwright against the mock and the real server, desktop and phone
+- tools/                setup, mock server, conformance suite, fake model server, server checks, schema check, soak
+- third_party/          git submodules (codicons, libgit2; quickjs-ng, unused since plugins run C4 code - docs/99 #6)
+- .ied/                 IED's own committed solution folder (skills); .ied/local/ is gitignored
+- vendor/, toolchains/, build/   created by setup and builds; gitignored
```

## Key conventions (details in `docs/`)

- **Split server + web client; the server owns all state** (even unsaved buffers). Close the browser, switch to the phone, reconnect - everything resumes.
- **No language/engine specifics in the host.** C4 and game engines get support through plugins (often purely declarative), LSP, DAP and MCP.
- **Visual Studio parity** for the IDE (menus, keymap, Error List, Output dropdown, debugger windows, toolbar tasks with dropdown-injected `${variables}`).
- **AI that works unattended**: role-based model groups, slot-aware routing with privacy guarantees, a harness that never "stops randomly" without saying why, Mission Control for many parallel agents, verified results.
- **Builds are driven by Nuke** (or any build system a plugin configures) - never reimplemented in the IDE.
- **`.ied/` at the solution root is committed** (settings, skills, modes); `.ied/local/` is per machine.
- **Theme:** dark, grayscale base; color only where it carries meaning; tokens only.
