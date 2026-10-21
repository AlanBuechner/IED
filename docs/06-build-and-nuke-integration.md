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

- **Variables** use `${name}` (the earlier `{name}` sketch is retired): built-ins (`solutionDir`, `projectRoot`, `project`, `config`, `arch`, `file`, `line`, `selection`, `startupProject`, `testFilter`...), project/preset variables, `env:`, `config:`, `input:`. Unknown variables fail the task visibly.
- **Dropdowns** have fixed `values` or `valuesFrom` (a command or binding, e.g. a future `nuke list --json`); `scope: solution` shares one selection across projects (VS-style solution configuration).
- **Steps:** `run` (argv preferred, or a shell string the system's shell runs as written - `cmd.exe /d /s /c`, so quotes and batch files such as `nuke.cmd` work), `task` (compose tasks), `debug` (start a DAP session from a named configuration), `binding` (call a plugin). Each has `cwd`, `env`, `output`, `problemMatcher` or `diagnosticsFile`, `testResults`, `onlyIf` (`prevSuccess` default, `prevFailure`, `always`), `when` expressions, `timeoutSeconds`, `background.readyPattern`, `target` (`host` or `sandbox:<name>`).
- **Slots** bind tasks to VS keys and menus: `debug` = F5, `run` = Ctrl+F5, `build` = Ctrl+Shift+B, plus `rebuild`, `clean`, `test`, `testSelected`, `debugTest`, `profile`.
- **Presets:** the built-in `ied.nuke` plugin contributes the `nuke` preset - `config` (Configuration: Debug, Development, Dist) and `arch` (Platform: x64) shared by the solution, and Build, Rebuild, Clean, Run and Test tasks in those slots, each `"${config:nuke.command}" <verb> ${config} ${projectName} -a ${arch}` (the `nuke.command` setting defaults to `nuke` on `PATH`); the toolbar is `config`, `arch`, `|`, Run, Build, Test. A language's plugin extends it as `<language>.nuke` (`extends: "nuke"`, `docs/26` section 6) with a `problemMatcher` for every step and its launch configuration: `c4.nuke` (the toolchain's `c4` plugin: the `c4` and `c4-test` matchers, a C4DAP Launch) and `cpp.nuke` (`ied.cpp`: the `clang` and `msvc` matchers, an LLDB Launch). `nuke init --ied` names `<language>.nuke` in each project's file, or `nuke` for a project without a language.
- Nothing here is C4-specific: Nuke is just the first build system a plugin configures.

## Protocol and state (M3)

What a project offers is its file's `variables`, `tasks`, `toolbar` and `debug` over those of its `presets` (each preset's entries first, the project's overriding them by key); a project without presets gets those whose `detect` glob matches a file in its root (`nuke.toml` for `c4.nuke`). The **startup project** (VS) is the one the toolbar, F5, Ctrl+F5 and Ctrl+Shift+B act on: the first executable project until someone picks another.

Selections are per machine, in `.ied/local/selections.json`: the startup project and each variable's value (a `scope: solution` variable once for the solution, others per project). Every device sees the same selections; a variable without one has its `default`, else its first value.

| Method | Params | Result |
|---|---|---|
| `task.list` | `wsId, project?` (the startup project) | `{project, startupProject, projects, variables: [{key, kind, label, values, value, scope}], tasks: [{key, label, icon, tooltip, slot}], toolbar, debug: {default, configurations: [name]}, problems}`; `projects` are those that offer tasks; `debug` names the debug configurations (`docs/25`) |
| `task.select` | `wsId, project?, key, value` | the new `task.list`; a value a dropdown does not offer is `bad_request` |
| `task.setStartup` | `wsId, project` | the new `task.list` |
| `task.run` | `wsId, project?, task` or `slot`, `context?` (`{file, line, selection, testFilter}` for the variables of that name) | `{runId, topic, output}` at once (`output`: the output source its first step writes); `not_found` for no such task or no task in that slot |
| `task.cancel` | `runId` | `{status}`: the run's process trees are killed |
| `task.runs` | `wsId` | `{runs: [{runId, project, task, label, status, exitCode, startedMs, endedMs}]}`, the last 50 |

Topics: `tasks:<wsId>` (snapshot `{offer, runs, ended}`: `task.list` of the startup project, the active runs, and the run that ended last or null - so a window that comes back shows a failed build in red; events `tasks.changed`, `run.started`, `run.ended`), and `run:<runId>` as in `docs/10`. A run is the server's: it goes on when every browser closes, and any device can cancel it. An untrusted workspace runs no task (`docs/11`): `task.run` answers `forbidden`.

A run expands variables when it starts (an unknown one fails it, naming the variable), then, with `saveBefore` (default `all`), saves the workspace's dirty documents itself (the server owns them, so no client needs to be open). Steps run in order: `run` as a child process in a job object (cancel and `timeoutSeconds` kill the tree), its output streamed to `run:<runId>` and to the step's output source (`build` by default, `run` for the `run` slot); `task` runs another task of the same project (a cycle fails the run); `debug` and `binding` arrive with the DAP pool (`docs/25`) and bindings (`docs/02`). `onlyIf` looks at the last step that ran (`prevSuccess`, the default, runs after a success; `prevFailure` after a failure; `always` either way; a `task` step's `onlyIf` decides whether the task it names starts) and `when` (the when-clause grammar over the run's variables) at the variables; the run fails if any step that ran failed, with that step's exit code. `concurrency`: `queue` waits for the task's previous run, `single` refuses a second, `restart` cancels the first, `parallel` runs both.

Problem matchers apply line by line to a step's output (both streams); a match becomes a diagnostic on `diag:<wsId>` with the matcher's `source` (default `build`), its path resolved by the matcher's `fileLocation` (absolute; relative to its roots; or searched in the step's `cwd`, the project root and the build's include paths, in that order). A run replaces the diagnostics its matchers' sources had for that project; they stay until the project's next run.

## Execution and output

- Every process step runs through the host supervisor (`src/terminal`): a job object per task (cancel kills the whole tree), streamed stdout/stderr, exit codes. The AI's `build_run`/`task_run` tools run the very same tasks (tagged with the chat).
- Steps write to **output sources** (`build`, `run`, `debug`, `test`, or plugin-contributed); the Output window selects a source from a dropdown (VS) and renders ANSI colors.
- Running state: spinner on the button, status bar "Building <project>... 0:12", Build > Cancel (Ctrl+Break); failure turns the status bar red with the first error; clicking jumps to it.

## Errors: problem matchers and structured diagnostics

- **Problem matchers** (contributed by plugins) parse output lines into Error List entries. The `c4` matcher handles `<path> (<line>:<col>) <severity>: <msg>`; `c4-test` handles `FAIL: <file>:<line>: <msg>`.
- **Path resolution** tries, in order: an absolute path, the step's `cwd`, the matcher's `fileLocation.roots` (expanded like a step's text, plus `${cwd}`; the `c4` matchers name `${projectRoot}/src`, as the compiler prints names relative to the source folder), the project's root, then the workspace's; a file that is in none of them, or outside the workspace, is no entry. (Planned: each include path from the build-derived LSP options as `${lspIncludePaths}`, and ambiguous matches flagged.)
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
