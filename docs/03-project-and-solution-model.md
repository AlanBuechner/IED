# 03 - Project and solution model

## Purpose

Define IED's workspace formats: Visual Studio-style solutions and projects, with the twist that **project files can live elsewhere than the code they describe** (generated project files sit under `build/` instead of polluting source folders), plus folder/file modes and federations. Schemas: `shared/host-api/schemas/`. Examples: `shared/host-api/examples/`. Features: `docs/features/03-workspaces.md`.

## Workspace kinds

| Kind | File | Notes |
|---|---|---|
| Solution | `<Name>.iedsln` | groups projects; holds local-plugin pins and the generator recipe |
| Folder | - | `ied ./`; `.ied/` created lazily only when something workspace-tier is saved |
| File | - | `ied file.txt`; lightweight folder workspace with just that document |
| Federation | `<Name>.iedfed` | several solutions/folders (e.g. C4 + a game engine + IED) in one workspace; each member keeps its own `.ied/`, trust, plugins, settings (interpretation of the plan's "federated projects", `docs/99` O1) |

**Files.** Each format has one extension of its own - `.iedsln`, `.iedproj`, `.iedfed` - so Windows can hand IED's files to IED: the desktop app registers `.iedsln` and `.iedfed` for the user, and a double click opens one in IED (`docs/27`). They were `<Name>.ied.sln`, `.ied.proj` and `.ied.fed` until 2026-10 (Windows took `.sln` for Visual Studio's); those names are still read everywhere - opening, the watcher, a solution's project globs as written, the schema checks - and `nuke init --ied` replaces the files it generated under them (`docs/23`). New files use the new names.

## Solution file (`solution.v1.schema.json`)

Fields: `version`, `name`, `projects[]` (paths or globs to `.iedproj`, optionally `optional` for generated files not yet generated), `groups[]` (nested display folders), `items[]` (Solution Items: README, AGENTS.md...), `localPlugins[]` (path + expected id; trusted workspaces only), `generator` (the command that regenerates project files, e.g. `nuke init --ied`, and which paths it owns - so IED offers *Regenerate* and warns before hand-editing generated files).

## Project file (`project.v1.schema.json`)

Fields: `version`, `name`, **`root`** (code root relative to the project file - the key feature), `include`/`exclude`, `kind` (executable/library/plugin/tests/other), `buildSystem` (opaque except `id`; e.g. `{ "id": "nuke", "projectName": "Game" }`), `presets` (task presets from plugins, e.g. `c4.nuke`), `variables` (toolbar dropdowns/toggles), `tasks` (named step lists bound to standard slots like `build`, `run`, `debug`, `test`, `profile`), `toolbar` (ordered variable and task keys), `debug` (launch/attach configurations), `lspProfiles`, `env`, `sandbox`, `references`.

Because `root` is explicit, a solution can keep every `.iedproj` under `build/ied/` (generated, gitignored) while code folders stay clean. The Solution Explorer shows each project's files from its `root`, grouped by the solution's `groups`. Task grammar and the build-system seam: `docs/06`.

## The server as built (M1)

`src/solutions/` reads a solution, and `src/services/workspaces` opens one:

- **Opening.** `ied path\MyGame.iedsln`, the control pipe's `open`, or `--workspace` opens a solution as a workspace of kind `solution`, rooted at the file's folder and named after the solution. Opening it again gives the same workspace. A folder holding one solution file opens as that solution (two or more: as the folder); any other folder opens as kind `folder`, and a file as kind `file` (its folder). The Solution Explorer shows a solution's projects or, on Switch Views, its folder (`docs/15`).
- **Validation.** The solution and each project file are JSONC, validated against `solution.v1` and `project.v1`.
- **Projects.** `projects[].file` may be a glob (`*`, `?`, `**`, `{a,b}`; `src/fs/glob`, which Find in Files, `search_text` and `fs_glob` match with too). An entry that finds no file is a problem unless it is `optional`. Each project's `root` is resolved against its own file, and a root that is not a folder is a problem; so are two projects with one name.
- **Problems** are listed per file with JSON pointers. A broken project stays listed with its problems.
- **The model** (`name, file, problems, groups, items, generator, localPlugins, projects: [{name, file, root, kind, problems, data}]`) is in the `ws:<wsId>` snapshot as `solution`.
- **Live.** When the solution or a project file changes, or a project file appears or goes, the model is read again and `solution.changed {solution, kind, name}` is published; a change IED makes itself (New Project, "Managing projects" below) reads it again at once, without waiting for the watcher. Project-scoped language servers whose project went or moved stop, and the open documents go to the servers that serve them now.
- **The project settings tier** is `.ied/projects/<Name>.json`, one per project (`docs/07`).
- **Local plugins** (`localPlugins`) load only while the workspace is trusted, and the manifest's id must be the one the solution names (`docs/02`).

## The `.ied/` folder (at the solution root)

Committed, so deleting generated files never loses configuration ("if we delete the generated files the settings and skills will not be lost" - IDEplan):

```
.ied/
+- solution.json            solution-tier settings (default model group, egress policy, toolbar defaults, permissions rules)
+- solutions/<Name>.json    per-solution settings when several solutions share one folder
+- projects/<Name>.json     project-tier settings
+- skills/<name>/SKILL.md   project skills (keyword-searchable by the AI; they hide a plugin's or the app's skill of the name)
+- plugins/<id>/            the workspace's own plugins (a custom language's, a project's tools), loaded once it is trusted (docs/26)
+- modes/<id>.json          custom agent modes (agent-mode.v1 schema)
+- privacy.json             sensitive-path globs for AI taint tracking
+- dictionary.txt           project spell-check words
+- memory/                  project memory facts (approved AI additions)
+- evals/                   eval tasks for the model arena
+- sandbox/<name>/          Dockerfile + setup.sh per sandbox
+- local/                   per-machine, gitignored:
   +- chats/                  chat transcripts, run journals, trajectories, media, checkpoints
   +- buffers/                journal of unsaved edits (hot exit)
   +- layout/                 layouts per device class, open documents, toolbar selections, startup project
   +- breakpoints.json, bookmarks.json
   +- lsp-options/<id>.json   build-derived LSP initialization options (written by the build system)
   +- captures/               profiler traces
```

Rules:

- Generators (`nuke init --ied`, templates) write `.ied/` contents **idempotently and never clobber hand edits** (`nuke init` already creates files only when missing).
- LSP initialization options are **derived from the build** (exact include paths and defines the compiler saw), regenerated after each successful build, and live in `local/` because they contain absolute paths.
- Trust is never stored in the repo (a repo cannot trust itself); it lives in app data (`docs/11`).
- The repo's `.gitignore` gets `.ied/local/` (generators add it).

## How this maps to Nuke

Nuke reads `nuke.toml` manifests (a `[workspace]` at the root, a `[project]` per project; `Nuke/README.md`) and owns *how to build*. IED's files are the IDE-facing index of *what exists and where*. `nuke init --ied` (`Nuke/README.md` "IED solution") writes the solution with the toolchain's `c4` plugin pinned in `localPlugins`, its `groups` from the projects' `group` (`group = "C4"` puts a project in the C4 folder, `"Engine/Tools"` nests; this repository's ten C4 compiler projects share the C4 folder, the C/C++ plugin's libraries `C++ Libraries`), generated project files under `build/ied/`, and the `.ied/` scaffold (skills, `.ied/local/` gitignored); running it again follows added and removed projects and leaves hand-edited files alone. A generated project file is named as Nuke names the project (`buildSystem.projectName`) and names the `c4.nuke` preset, whose tasks pass that `${projectName}` to `nuke` with the selected configuration (`docs/06`); a project-scoped language server profile sees the same `${projectName}` (`docs/24`). Nuke stays authoritative; IED never re-derives build inputs (`docs/99` D6), though it may read the declarative manifests for display.

## New projects

New Project (owner, 2026-10-22: "UI to create a new project with templates for c/c++, c4, go", each language plugin adding its own) makes a project from a template a plugin contributes (`projectTemplates`, `docs/02`): a **new workspace** - a folder `<location>/<name>`, which opens trusted (its owner made it) - or a project **added to the open solution** when the solution's folder holds one of the template's `into` files. A template's `steps` run first, each to its end, in `${workspaceDir}` by default: the new folder, or the solution's; then its `files` folder is copied into the project's folder with `${name}` filled in paths and text, and a file already there stays (the steps' own init wins, and hand edits are never clobbered). A step that fails removes the half-made folder and the error says why, with the step's output and, for a program a setting names, how to give it (`docs/06` "A missing program").

Every shipped template is the build system's own init and the language's starter files: `nuke init ${name} --language <id> --ied` (`docs/23` N-20) makes the workspace - or, run in a Nuke workspace, the project `./<name>` - and its IED solution, so the project builds, runs and debugs from the toolbar like any Nuke project, and `into: ["nuke.toml"]` lets it join a Nuke solution. Shipped (2026-10-22): `ied.cpp` C++ Console App, C Console App and C++ Static Library (the C/C++ driver finds the LLVM release of the IED checkout when the workspace names none); `ied.go` Go Console App (`go.mod` and `main.go`); `c4` Console App and Module (`nuke init`'s own sources: a Console App's `src/main.cf` reads its arguments and writes through `Core::Format`, and `src/tests/main_tests.cf` holds an example test). C4's come with the toolchain's plugin, so they show wherever a workspace that pins it is open on the server - the start page included.

As built (`src/projects/templates.cf`): `project.templates {wsId?}` lists the templates of the global plugins and of each trusted open workspace's own plugins, each once (`docs/10` "Projects"); `project.create` makes one, releasing the server lock while the steps run (two minutes each at most). The dialog is the shell's (`frontend/src/shell/newproject.js`, `docs/15` "New Project"), since the start page has no workspace and so no plugin UI.

## Managing projects

The owner (2026-10-22: tool calls "for project management like making new projects moving them removing them etc as well as taking an existing folder and making an IED project out of it", which "need to affect the current running loaded instance of the project so i can see them in the solution explorer without needing to reload") - and through the protocol any client - changes a solution's projects on the server:

- **Import** (`project.import`): an existing folder made an IED solution. An open folder workspace there **becomes the solution in place**: same `wsId`, chats, terminals and documents; `solution.changed` carries its new `kind` and `name`, the server topic's `workspace.changed` its summary, and the recent projects name the solution instead of the folder. Its per-machine state stays in the app data for this run and moves into the solution's `.ied/local/` the next time it opens (`docs/07` "Workspace state"). A folder that is not open opens as a new workspace, as trusted as the folder was. A folder that holds a solution file already (a build system's generator wrote it while the folder was open as a folder, as `nuke init --ied` does) is opened as that solution, unchanged, with a note saying so; only a workspace already open as a solution is refused (`conflict`).
- **Add** (`project.add`): a folder of the workspace made a project of its solution; a folder workspace becomes a solution first, with no project for its own folder.
- **Remove** (`project.remove`): a project taken out of the solution and its build, its folder kept, or moved to the trash with `deleteFiles` (recoverable, `docs/10` "Files"). Refused while another project's `references` name it.
- **Move** (`project.move`): a project's folder moved to another folder of the workspace; open documents follow it (`doc.renamed`), as with `fs.rename`.

Where the change is made depends on who owns the solution's projects:

- **A project system** - a plugin's `projectSystems` entry (`docs/02`) whose `detect` file is in the solution's folder - owns a solution its build system generates. Its operation's steps change the build system's own manifests, then its `generate` writes the IED files again (IED never edits generated files, and never reimplements the build system, AGENTS rule 3). The `nuke` plugin's: `generate` is `nuke init --ied`; `add`, `remove` (and `delete`, before IED trashes the folder) and `move` are `nuke project add|remove|move` (`docs/23` N-22), which edit `nuke.toml` in place with its comments kept. Steps run the build system, which may run the workspace's scripts, so a project system needs a trusted workspace (`forbidden` otherwise). A move runs after IED moved the folder (so documents follow); a failing move step puts the folder back.
- **IED** owns a solution written by hand: it writes `<Name>.iedsln` and `<folder>/<Name>.iedproj` (`root` "."), lists and unlists project files in `projects` and names in `groups` with the file's comments kept (`settings/jsonedit`), rewrites a project file's `root` when the project file stays behind, and trashes a project file a glob of `projects` would still list. A new project file's `language` is the first `projectIcons` entry whose marker lies in the folder, else the language most of its files are in among those `projectIcons` draw; its `presets` are the task presets whose `detect` file lies there, of plugins of that language or of none, less those another of them extends; its `kind` is `executable` with a preset and `other` without (the caller may give all three). What IED chose comes back as notes.
- **Anything else** - a solution whose `generator` no loaded project system runs - is refused with the generator's command: its build system's files are what to change.

As built (`src/projects/manage.cf`, `detect.cf`, `solutionfile.cf`; the steps shared with templates in `src/projects/steps.cf`): every method answers `{workspace, path, project, trashId?, notes, log}` (`docs/10` "Projects"). Plugins call the same operations through `api/projects.ch` (`docs/26`, permission `workspace.projects`), the AI through the chat's `projects` tools (`docs/13`), and the owner through the Solution Explorer's menus (`docs/15`: Add Existing Folder as Project..., Add Folder as Project, Move Project..., Remove). Checked by `tools/server-checks/tests/test_project_management.py` (hand-written solutions, a fake project system with a failing move, the real `nuke` plugin and `nuke` commands, the chat's tools), unit cases `server.projects` and `server.localstate`, and Nuke's `test_project.py`.

## Legacy workspace folders

A folder may still hold the predecessor IDE's hidden workspace folder. IED never writes it; `ied import legacy` converts its settings, skills, modes and chats once (`docs/20`). Nuke writes only IED's bridge (`.ied/local/lsp-options/<project>.json`).
