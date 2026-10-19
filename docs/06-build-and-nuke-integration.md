# 06 - Build and Nuke integration

## Purpose

IED does **not** reimplement a build system (AGENTS.md rule 3). It runs **tasks** - configured step lists - that invoke whatever the project uses; for C4 projects that is **Nuke** (`nuke.bat`). This doc defines the task model, how values flow from toolbar dropdowns into commands, where output goes, how errors become Error List entries, and what Nuke must add for a first-class experience. Features: `docs/features/05-build-run-test.md`. Schema: `shared/host-api/schemas/project.v1.schema.json`.

## What Nuke is today (2026-10-19)

- `nuke.bat` -> `Nuke/nuke.py`; actions `build | run | test | clean | list | init`; flags `-p <project>`, `-c <config>`, `-a <arch>`, `-s <system>`, `-ci` (compile info), `--setup`; configs `Debug | Development (default) | Dist` with aliases; a bare `nuke` builds.
- Projects are folders with `Project.py` (`GetProject()` returning build type, module links, includes, defines, links, copies...); the project name is the folder name. Output `build/{bin,int}/{config}-{system}-{arc}/{proj}` (Claymore's older copy uses `nuke-bin/`/`nuke-int/`).
- Output is plain text (no ANSI); **compiler output is printed in one block after each compile** (not streamed). Diagnostics: `<path> (<line>:<col>) error|warning|fatal error: <msg>` followed by the source line and a caret; `<path>` is **relative to its owning source root** (can be ambiguous across roots).
- After a successful build: `build-info.json` (per-project timings) and `build-trace.json` (Chrome trace) in the int dir.
- Tests: test projects (`src/tests/` present) are built with `-test`; output lines `[TEST] name`, `ok name (ms)`, `FAILED name`, `FAIL: file:line: msg`. The compiler supports `-test-json <path>` and test executables accept `--filter <glob>`, but **Nuke passes neither** yet.
- `EchoConfig.py` writes `.echo/plugins.json` (once) and `.echo/c4-lsp.json` (after each successful build, only when changed): `{ includePaths, defines, c4Home, projects }`.

## Task model

```jsonc
// in a .ied.proj (full example: shared/host-api/examples/Game.ied.proj)
"variables": {
	"config": { "kind": "dropdown", "values": ["Debug", "Development", "Dist"], "default": "Development", "scope": "solution" },
	"arch":   { "kind": "dropdown", "values": ["x64"], "default": "x64", "scope": "solution" }
},
"tasks": {
	"build": { "slot": "build", "icon": "$(gear)", "steps": [
		{ "run": ["${solutionDir}/nuke.bat", "build", "-p", "${buildSystem.projectName}", "-c", "${config}", "-a", "${arch}"],
		  "cwd": "${solutionDir}", "output": "build", "problemMatcher": "c4" } ] },
	"debug": { "slot": "debug", "icon": "$(debug-start)", "steps": [
		{ "task": "build" },
		{ "debug": "Game", "onlyIf": "prevSuccess" } ] }
},
"toolbar": ["config", "arch", "|", "debug", "build", "test"]
```

- **Variables** use `${name}` (the earlier `{name}` sketch is retired): built-ins (`solutionDir`, `projectRoot`, `project`, `config`, `arch`, `file`, `line`, `selection`, `startupProject`, `testFilter`...), project/preset variables, `env:`, `setting:`, `input:`. Unknown variables fail the task visibly.
- **Dropdowns** have fixed `values` or `valuesFrom` (a command or binding, e.g. a future `nuke list --json`); `scope: solution` shares one selection across projects (VS-style solution configuration).
- **Steps:** `run` (argv preferred, or a shell string), `task` (compose tasks), `debug` (start a DAP session from a named configuration), `binding` (call a plugin). Each has `cwd`, `env`, `output`, `problemMatcher` or `diagnosticsFile`, `testResults`, `onlyIf` (`prevSuccess` default, `prevFailure`, `always`), `when` expressions, `timeoutSeconds`, `background.readyPattern`, `target` (`host` or `sandbox:<name>`).
- **Slots** bind tasks to VS keys and menus: `debug` = F5, `run` = Ctrl+F5, `build` = Ctrl+Shift+B, plus `rebuild`, `clean`, `test`, `testSelected`, `debugTest`, `profile`.
- **Presets:** the `c4` plugin contributes the `c4.nuke` preset (variables, toolbar, standard tasks), so a `nuke --init --ied` project needs only `"presets": ["c4.nuke"]` and overrides by key.
- Nothing here is C4-specific: Nuke is just the first build system a plugin configures.

## Execution and output

- Every process step runs through the host supervisor (`src/terminal`): a job object per task (cancel kills the whole tree), streamed stdout/stderr, exit codes. The AI's `build_run`/`task_run` tools run the very same tasks (tagged with the chat).
- Steps write to **output sources** (`build`, `run`, `debug`, `test`, or plugin-contributed); the Output window selects a source from a dropdown (VS) and renders ANSI colors.
- Running state: spinner on the button, status bar "Building <project>... 0:12", Build > Cancel (Ctrl+Break); failure turns the status bar red with the first error; clicking jumps to it.

## Errors: problem matchers and structured diagnostics

- **Problem matchers** (contributed by plugins) parse output lines into Error List entries. The `c4` matcher handles `<path> (<line>:<col>) <severity>: <msg>`; `c4-test` handles `FAIL: <file>:<line>: <msg>`.
- **Path resolution** tries, in order: absolute path, step `cwd`, project `root`, then each include path from the build-derived LSP options (`${lspIncludePaths}`) - needed because the compiler prints root-relative names. Ambiguous matches are flagged in the entry.
- **Structured diagnostics** beat regexes: if a step declares `diagnosticsFile`, IED reads JSON diagnostics instead. Nuke work item N5 adds this with absolute paths.
- Build diagnostics persist until the next build of that project; LSP diagnostics for the same location are de-duplicated.

## Tests

- Test Explorer reads `c4-test-json` result files (once Nuke passes `-test-json`, N4) and falls back to the text matcher before that.
- Run selected/failed tests by setting `${testFilter}` (Nuke must pass `--filter`, N4); debug a test by launching its harness under C4DAP with the filter.

## LSP/build sync (the `.echo/` bridge, reborn)

Nuke's `IEDConfig.py` (work item N1) writes `.ied/local/lsp-options/<id>.json` after each successful build with exactly the include paths and defines the compiler used (same content as `.echo/c4-lsp.json`). The `c4` plugin's LSP profile points `initializationOptionsFile` at it; IED watches the file and pushes changes (`didChangeConfiguration` or restart). Diagnostics therefore match a real build.

## Nuke work items (C4 repo)

N1 IEDConfig LSP options; N2 `nuke --init --ied` writes solution/project/`.ied/` scaffold; N3 stream compiler output; N4 `nuke test --json-results <path> --filter <glob>`; N5 `--diagnostics-json` with absolute paths; N6 `nuke list --json`; N7 optional ANSI color; N8 update Claymore's older copy (exit codes, `-g`, `--projects`); N9 keep `.echo/` output during the transition. Details and rationale: `docs/17`.

## Decided

- `.ied.proj` is **not** read by Nuke; Nuke stays the build authority and generates IED files (`docs/99` D6).
- Build insights (`build-trace.json`) open in the profiler timeline (P2).
- Multi-project builds are the build system's job; IED runs one task.
