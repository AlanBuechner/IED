# 03 - Project and solution model

## Purpose

Define IED's workspace formats: Visual Studio-style solutions and projects, with the twist that **project files can live elsewhere than the code they describe** (generated project files sit under `build/` instead of polluting source folders), plus folder/file modes and federations. Schemas: `shared/host-api/schemas/`. Examples: `shared/host-api/examples/`. Features: `docs/features/03-workspaces.md`.

## Workspace kinds

| Kind | File | Notes |
|---|---|---|
| Solution | `<Name>.ied.sln` | groups projects; holds local-plugin pins and the generator recipe |
| Folder | - | `ied ./`; `.ied/` created lazily only when something workspace-tier is saved |
| File | - | `ied file.txt`; lightweight folder workspace with just that document |
| Federation | `<Name>.ied.fed` | several solutions/folders (e.g. C4 + a game engine + IED) in one workspace; each member keeps its own `.ied/`, trust, plugins, settings (interpretation of the plan's "federated projects", `docs/99` O1) |

## Solution file (`solution.v1.schema.json`)

Fields: `version`, `name`, `projects[]` (paths or globs to `.ied.proj`, optionally `optional` for generated files not yet generated), `groups[]` (nested display folders), `items[]` (Solution Items: README, AGENTS.md...), `localPlugins[]` (path + expected id; trusted workspaces only), `generator` (the command that regenerates project files, e.g. `nuke init --ied`, and which paths it owns - so IED offers *Regenerate* and warns before hand-editing generated files).

## Project file (`project.v1.schema.json`)

Fields: `version`, `name`, **`root`** (code root relative to the project file - the key feature), `include`/`exclude`, `kind` (executable/library/plugin/tests/other), `buildSystem` (opaque except `id`; e.g. `{ "id": "nuke", "projectName": "Game" }`), `presets` (task presets from plugins, e.g. `c4.nuke`), `variables` (toolbar dropdowns/toggles), `tasks` (named step lists bound to standard slots like `build`, `run`, `debug`, `test`, `profile`), `toolbar` (ordered variable and task keys), `debug` (launch/attach configurations), `lspProfiles`, `env`, `sandbox`, `references`.

Because `root` is explicit, a solution can keep every `.ied.proj` under `build/ied/` (generated, gitignored) while code folders stay clean. The Solution Explorer shows each project's files from its `root`, grouped by the solution's `groups`. Task grammar and the build-system seam: `docs/06`.

## The server as built (M1)

`src/solutions/` reads a solution, and `src/services/workspaces` opens one:

- **Opening.** `ied path\MyGame.ied.sln`, the control pipe's `open`, or `--workspace` opens a solution as a workspace of kind `solution`, rooted at the file's folder and named after the solution. Opening it again gives the same workspace. A folder opens as kind `folder`, and a file as kind `file` (its folder).
- **Validation.** The solution and each project file are JSONC, validated against `solution.v1` and `project.v1`.
- **Projects.** `projects[].file` may be a glob (`*`, `?`, `**`; `src/fs/glob`). An entry that finds no file is a problem unless it is `optional`. Each project's `root` is resolved against its own file, and a root that is not a folder is a problem; so are two projects with one name.
- **Problems** are listed per file with JSON pointers. A broken project stays listed with its problems.
- **The model** (`name, file, problems, groups, items, generator, localPlugins, projects: [{name, file, root, kind, problems, data}]`) is in the `ws:<wsId>` snapshot as `solution`.
- **Live.** When the solution or a project file changes, or a project file appears or goes, the model is read again and `solution.changed {solution}` is published.
- **The project settings tier** is `.ied/projects/<Name>.json`, one per project (`docs/07`).
- **Local plugins** (`localPlugins`) load only while the workspace is trusted, and the manifest's id must be the one the solution names (`docs/02`).

## The `.ied/` folder (at the solution root)

Committed, so deleting generated files never loses configuration ("if we delete the generated files the settings and skills will not be lost" - IDEplan):

```
.ied/
+- solution.json            solution-tier settings (default model group, egress policy, toolbar defaults, permissions rules)
+- solutions/<Name>.json    per-solution settings when several solutions share one folder
+- projects/<Name>.json     project-tier settings
+- skills/<name>/SKILL.md   project skills (keyword-searchable by the AI)
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

Nuke reads `nuke.toml` manifests (a `[workspace]` at the root, a `[project]` per project; `Nuke/README.md`) and owns *how to build*. IED's files are the IDE-facing index of *what exists and where*. `nuke init --ied` (`Nuke/README.md` "IED solution") writes the solution with the toolchain's `c4` plugin pinned in `localPlugins`, generated project files under `build/ied/`, and the `.ied/` scaffold (skills, `.ied/local/` gitignored); running it again follows added and removed projects and leaves hand-edited files alone. A generated project file is named as Nuke names the project (`buildSystem.projectName`) and names the `c4.nuke` preset, whose tasks pass that `${projectName}` to `nuke` with the selected configuration (`docs/06`); a project-scoped language server profile sees the same `${projectName}` (`docs/24`). Nuke stays authoritative; IED never re-derives build inputs (`docs/99` D6), though it may read the declarative manifests for display.

## Legacy workspace folders

A folder may still hold the predecessor IDE's hidden workspace folder. IED never writes it; `ied import legacy` converts its settings, skills, modes and chats once (`docs/20`). Nuke writes only IED's bridge (`.ied/local/lsp-options/<project>.json`).
