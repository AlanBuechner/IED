# 06 — Build & Nuke integration

## Purpose

IED does **not** reimplement a build system. For C4 repos it drives **Nuke** (`C:\GameDev\C4\Nuke`, ported into this repo's `Nuke/` kit). This doc defines the seam: what IED configures, how commands are built, where output goes, and how LSP stays in sync with real builds.

## Why Nuke (recap of what we're reusing)

From studying `C:\GameDev\C4\Nuke`:

- **Launcher:** repo-root `nuke.bat` delegates to `Nuke/nuke.py`. Positional actions (`build|run|test|clean|list [config]`) plus long flags; config aliases (`dev`→Development); default config **Development**.
- **Config.py** owns: configurations (`Debug`, `Development`, `Dist`), architectures (`x64`), systems (`windows`), output layout `build/{bin,int}/{config}-{system}-{arc}/{projName}`, compiler path, extra project folders.
- **ProjectManager** auto-discovers projects by walking for `Project.py`; maintains the module graph with cycle detection; sources are `.cf`/`.ch`.
- **ObjectBuilder** does incremental decisions (args cache + dependency tree); **TestRunner** discovers test-bearing projects (any executable with `src/tests/`) and aggregates exit codes.
- **EchoConfig.py** is today's editor bridge: writes `.echo/plugins.json` (plugin recipe) and `.echo/c4-lsp.json` (build-derived LSP init options: exact `-I` include paths + defines), regenerated idempotently on every build so LSP diagnostics match a real compile.

IED keeps all of that. The port into this repo is mechanical once the C4 compiler can build it: copy the kit, point `Config.py` at IED's projects, add the new bridge (below).

## Command templates & toolbar injection

Toolbar buttons (see 05/editor) are **lists of command steps** with template variables filled from toolbar dropdowns and project state:

```jsonc
// in .ied.proj "toolbars"
{
  "id": "main",
  "items": [
    { "kind": "dropdown", "id": "config", "values": ["Debug", "Development", "Dist"], "default": "Development" },
    { "kind": "dropdown", "id": "arch",   "values": ["x64"], "default": "x64" },
    { "kind": "button", "id": "build", "icon": "build.svg",
      "steps": [
        { "command": "nuke.bat build -p {project} -c {config} -a {arch}", "output": "build" }
      ] },
    { "kind": "button", "id": "run", "icon": "run.svg",
      "steps": [
        { "command": "nuke.bat build -p {project} -c {config} -a {arch}", "output": "build" },
        { "command": "nuke.bat run   -p {project} -c {config} -a {arch}", "output": "run", "onlyIf": "prevSuccess" }
      ] },
    { "kind": "button", "id": "test", "icon": "test.svg",
      "steps": [ { "command": "nuke.bat test -c {config}", "output": "test" } ] }
  ]
}
```

- `{project}`, `{config}`, `{arch}`, `{system}` are built-in variables; dropdowns define custom ones.
- `onlyIf: prevSuccess` gives the plan's "build, then if successful run" behavior generically — no C4 knowledge in IED.
- Each step runs through the **terminal supervisor** (streaming output), so toolbar commands, AI shell calls and the human terminal share one process manager. Exit codes drive `onlyIf` and the success/failure status bar.
- The build system id in `.ied.proj` (`"system": "nuke"`) selects which command vocabulary applies; other build systems plug in via their own template conventions later. Nothing here is C4-specific — Nuke is just the first registered build system.

## Output routing

- Steps declare `"output": "<tabId>"`; the editor plugin's output window routes each step's stdout/stderr (ANSI preserved) to that tab: `build`, `run`, `test` by default, any plugin can register more.
- Long-running steps show a live spinner on the button; failure colors the status bar red with the first error line; clicking it jumps to the output tab at the error.

## LSP/build sync (the `.echo/` bridge, reborn)

Today's `EchoConfig.py` pattern carries over unchanged in spirit:

- Nuke's IED port gains an **IEDConfig** step: after each successful build, write `.ied/local/lsp-options/<proj>.json` with exactly the include paths and defines passed to the compiler (same logic as current `WriteLspOptions`).
- The C4 plugin's LSP profile references that file via `initializationOptionsFile` — so the language server always sees the same compiler input a real build uses, and diagnostics match.
- `nuke --init` (ported) writes the IED project/solution files + `.ied/` scaffold for new projects, idempotently, never clobbering hand edits.
- The LSP options file lives in `.ied/local/` because it's machine-specific (absolute paths); everything else in `.ied/` is committed.

## Test integration

- `nuke test` runs every test-bearing project; IED streams per-project results to the `test` output tab and surfaces a summary (pass/fail counts) in the status bar.
- Per-test granularity: when Nuke's test runner gains structured output (JSON), the editor plugin renders a VS-style Test Explorer view. Until then, console text is enough — don't build the UI before the data exists.

## Open items

- Should `.ied.proj` eventually be *read by Nuke* (single source of truth) instead of IED translating to `-p/-c/-a` flags? Leaning no for v1 — keep Nuke untouched and IED as a thin caller; revisit when the C4 repo work happens.
- `--ci` (per-file compile times) output: route to build tab now, consider feeding the profiler plugin later.
- Multi-solution builds (building more than one project at once from the toolbar): v1 = sequential steps; parallelization is a Nuke concern, not IED's.
