# 06 - Build and Nuke integration

## Purpose

IED does **not** reimplement a build system (AGENTS.md rule 3). It runs **tasks** - configured step lists - that invoke whatever the project uses; for C4 projects that is **Nuke** (`nuke`, `Nuke/README.md`). This doc defines the task model, how values flow from toolbar dropdowns into commands, where output goes, how errors become Error List entries, and what Nuke must add for a first-class experience. Features: `docs/features/05-build-run-test.md`. Schema: `shared/host-api/schemas/project.v1.schema.json`.

## What Nuke is (2026-10-20)

Nuke was rebuilt when C4 moved into this repository; reference `Nuke/README.md`, design `docs/23`.

- One copy in this repository; `nuke` on PATH through `bin/nuke.cmd`. Workspaces pin a toolchain version from the shared store (`toolchains/c4/<version>`, `dev` = the compiler built from `C4/`), so no project vendors Nuke or C4.
- Words chain: `nuke build run dev`, `nuke test --filter "math.*"`, `nuke kit dist`; flags `-p <project>` (repeatable), `-c <config>`, `-a`, `-s`, `-w <workspace>`, `--all`, `--json`, `-v`; arguments after `--` go to the program. Configs `Debug | Development (default) | Dist` with aliases; a bare `nuke` builds.
- Manifests are TOML: `nuke.toml` with `[workspace]` at the root and `[project]` per project (`kind` executable/module/custom, `depends`, `sources`, `defines`, `links`, steps, copies, per-config overrides). IED can read them directly; Nuke stays the build authority (D6).
- Every command has `pre-X`/`X`/`post-X` phases that projects attach steps to (asset cooking, codegen, packaging); workspaces add commands (`[commands.kit]`) and Python extensions.
- Output `build/{bin,int}/{config}-{system}-{arc}/{proj}`; compiler output **streams**; diagnostics keep the compiler's format `<path> (<line>:<col>) error|warning|fatal error: <msg>` with root-relative paths (N5 still open). `build/int/<triple>/nuke-trace.json` records step and project timings (Chrome trace format).
- Tests: `nuke test` builds each test-bearing project into `<P>-tests.exe` with `-test -test-json <int>/tests/results.json`, passes `--filter`, and runs every harness; output lines `[TEST] name`, `ok name (ms)`, `FAILED name`, `FAIL: file:line: msg`.
- `nuke list --json` / `nuke info --json` describe projects, configurations, toolchain and commands for toolbar dropdowns (`valuesFrom`).
- Exit codes: 1 for manifest/command-line errors, otherwise the failing tool's or program's own code.

## Task model

```jsonc
// in a .ied.proj (full example: shared/host-api/examples/Game.ied.proj)
"variables": {
	"config": { "kind": "dropdown", "values": ["Debug", "Development", "Dist"], "default": "Development", "scope": "solution" },
	"arch":   { "kind": "dropdown", "values": ["x64"], "default": "x64", "scope": "solution" }
},
"tasks": {
	"build": { "slot": "build", "icon": "$(gear)", "steps": [
		{ "run": ["nuke", "build", "-p", "${buildSystem.projectName}", "-c", "${config}", "-a", "${arch}"],
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
- **Presets:** the `c4` plugin contributes the `c4.nuke` preset (variables, toolbar, standard tasks), so a `nuke init --ied` project needs only `"presets": ["c4.nuke"]` and overrides by key.
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

- Test Explorer reads the `c4-test-json` result files Nuke writes to `<int>/tests/results.json` (N4, done) and falls back to the text matcher.
- Run selected/failed tests by setting `${testFilter}` (`nuke test --filter ${testFilter}`); debug a test by launching `<P>-tests.exe` under C4DAP with the filter.

## LSP/build sync

After each successful compile Nuke writes `.ied/local/lsp-options/<project>.json` with exactly the include paths, defines, toolchain (`c4Home`), build description (`projects`) and target arguments the compiler used (N1, done; `Nuke/nuke/c4/ide_bridge.py`). The `c4` plugin's LSP profile points `initializationOptionsFile` at it; IED watches the file and pushes changes (`didChangeConfiguration` or restart). Diagnostics therefore match a real build.

## Nuke work items

Done 2026-10-20: N1 LSP options, N3 streamed output, N4 `nuke test --filter` + results JSON, N6 `nuke list --json`; N8 superseded (shared toolchains), N9 retired. Open: N2 `nuke init --ied` (solution/project files), N5 `--diagnostics-json` with absolute paths (needs the compiler), N7 optional ANSI color. Details: `docs/17`, `docs/23`.

## Decided

- `.ied.proj` is **not** read by Nuke; Nuke stays the build authority and generates IED files (`docs/99` D6). IED may read `nuke.toml` directly (declarative, `docs/23` N-1) for display, never to re-derive build inputs.
- Build insights (`build-trace.json`) open in the profiler timeline (P2).
- Multi-project builds are the build system's job; IED runs one task.
