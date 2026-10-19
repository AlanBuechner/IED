# 03 — Project and solution model

## Purpose

Define IED's project/solution format: Visual Studio-style grouping, with the twist that **project files can live elsewhere than the code they describe** (so generated project files can sit in a build directory instead of polluting source folders).

## Files

### Solution file: `<Name>.ied.sln` (JSON)

At the solution root. Groups projects and carries solution-level configuration.

```jsonc
{
  "version": 1,
  "name": "MyGame",
  "projects": [
    { "file": "build/projects/Core.ied.proj" },          // path relative to solution root
    { "file": "Engine/Claymore.ied.proj" }               // or anywhere else
  ],
  "groups": [                                            // optional display grouping in the tree
    { "name": "Engine",   "projects": ["Engine/Claymore.ied.proj"] },
    { "name": "Gameplay", "projects": ["build/projects/Core.ied.proj"] }
  ],
  "localPlugins": [                                      // project-local plugins, trusted-workspace only
    { "id": "c4", "path": "../C4/Nuke/tools/ied" }       // relative to solution root
  ],
  "settingsRef": ".ied/solution.json"                    // solution-tier settings (see 07)
}
```

### Project file: `<Name>.ied.proj` (JSON)

One per project. The key feature from the plan: **`root` may point to a folder other than the one containing this file.**

```jsonc
{
  "version": 1,
  "name": "Core",
  "root": "../src/Core",        // where the code actually is; defaults to "." (this file's folder)
  "build": {
    "system": "nuke",           // build system id; "nuke" = invoke nuke.bat (see 06). Generic slot for others.
    "projectName": "Core",      // how the build system knows this project
    "defaultConfiguration": "Development"
  },
  "toolbars": [                 // per-project toolbar config (dropdowns + buttons) — see 06
    { "id": "main", "items": [ ... ] }
  ],
  "lspProfiles": ["c4"],        // which registered LSP profiles apply to this project's files
  "settingsRef": ".ied/projects/Core.json"   // optional project-tier settings
}
```

Because `root` is explicit, a solution can keep all `.ied.proj` files under `build/projects/` (generated, gitignored) while the code stays clean — exactly the workflow the plan calls for. The file tree in the editor is rooted at each project's `root`, and the tree groups projects by the solution's `groups`.

## The `.ied/` hidden folder (solution root)

Mirrors how C4 repos use `.echo/` today (plugins.json + c4-lsp.json + skills). It is **committed** so deleting generated files never loses configuration:

```
.ied/
├── solution.json          # solution-tier settings (toolbar defaults, AI model group overrides, secure mode…)
├── projects/              # project-tier settings, one file per project name
│   └── Core.json
├── skills/                # project skills — markdown, keyword-searchable by the AI
│   ├── c4-language/SKILL.md
│   └── mygame-architecture/SKILL.md
├── plugins.local.json     # resolved local plugin pins (written by tooling when a kit like Nuke inits a project)
└── local/                 # per-machine state — gitignored, never committed
    ├── workspace.json     # open tabs, pane layout, last toolbar selections
    └── lsp-options/       # machine-specific LSP initialization options (build-derived, e.g. c4.json)
```

Rules:

- Tooling that generates project files (Nuke's `--init` port, a future `ied --init`) writes the `.ied/` contents **idempotently and never clobbers hand edits** — same discipline as Nuke's current `EchoConfig.py`.
- LSP initialization options are *derived from the build* (include paths + defines exactly as passed to the compiler) so diagnostics match a real build. That file is regenerated on every build; user files under `.ied/` are not touched by builds.
- Skills live here (not in `build/`) on purpose: "if we delete the generated files the settings and skills will not be lost" (IDEplan).

## How this maps to Nuke today

Nuke auto-discovers projects by walking for `Project.py` and reads `Config.py` at the repo root. IED does **not** replace that — it adds a layer: the solution/project files tell IED *what exists and where*, while Nuke keeps owning *how to build*. The project file's `build.projectName` is what gets passed as `-p` to `nuke.bat`. When both exist, Nuke's discovery is authoritative for build inputs; IED's files are the IDE-facing index. (See 06 for the exact interplay and one open question about whether Nuke should eventually learn to read `.ied.proj` directly.)

## Open items

- Final JSON schemas for `.ied.sln` / `.ied.proj` → `shared/host-api/` (blocking, see 99).
- Multiple solutions in one folder: allowed, each is just a file; the workspace opens exactly one at a time.
- Migration story for existing C4 repos (which have `.echo/`): does Nuke's init write both folders during a transition period? Decide when the C4 repo work starts.
