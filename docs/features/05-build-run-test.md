# Features: build, run, test, output (BLD)

Design: `docs/06` (Nuke integration, command templates). Owner: `plugins/editor` (UI) on top of `src/tasks` (host step runner) unless noted.

The generic mechanism is a **task**: a named list of **steps**, each a process (or a debug launch, or a binding call), with variables, conditions and an output target. Toolbar buttons, menu items (Build > Build Solution), keyboard shortcuts (F5, Ctrl+Shift+B), CodeLens actions, AI tools and project templates all run tasks. No build system is special; Nuke is the first one configured.

## Toolbar and task definitions

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-BLD-1 | Per-project toolbar from `.ied.proj` (`toolbars`): dropdowns (fixed values, or values produced by a command / binding, e.g. `nuke list`) and buttons with icons, tooltips, enablement | P0 | US-BLD-1 |
| F-BLD-2 | Variables: built-ins (`project`, `projectRoot`, `solutionDir`, `config`, `arch`, `system`, `file`, `fileDir`, `line`, `selection`, `startupProject`, `testFilter`), dropdown ids, `env:NAME`, `config:key`, `input:<prompt>` (asks once) | P0 | US-BLD-1 |
| F-BLD-3 | Step kinds: `process` (argv or shell string - argv preferred), `debug` (start a DAP session with a launch config), `binding` (call a plugin binding), `task` (run another task) | P0 (process), P1 (others) | US-BLD-2 |
| F-BLD-4 | Conditions: `onlyIf: prevSuccess | prevFailure | always`, plus `when` expressions over variables (`config == 'Dist'`) | P0 | US-BLD-2 |
| F-BLD-5 | Per-step output target (output source id), `cwd`, `env`, timeout, `problemMatcher` id, `background: readyPattern` (for "start server then continue") | P0 | US-BLD-3 |
| F-BLD-6 | Standard task slots the shell binds to keys and menus: `build` (Ctrl+Shift+B), `rebuild`, `clean`, `run` (Ctrl+F5), `debug` (F5), `test`, `testSelected`, `debugTest`, `profile`; a project fills any subset | P0 | US-BLD-1, US-DBG-1 |
| F-BLD-7 | Solution-level tasks (build all projects in dependency order the build system decides; IED just calls it) | P1 | - |
| F-BLD-8 | Toolbar presets contributed by plugins (the C4 plugin ships the Nuke preset so `nuke init` projects need no hand-written toolbar) (gen) | P0 | US-XRP-1 |
| F-BLD-9 | Save-all-before-build (default on, VS) | P0 | - |

## Running tasks

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-BLD-10 | Processes run through the host process supervisor (`src/terminal`): job object per task (cancel kills the whole tree), streamed stdout/stderr, exit code | P0 | US-BLD-5 |
| F-BLD-11 | Running state: spinner on the button, Build menu shows *Cancel* (Ctrl+Break, VS), status bar "Building <project>..." with elapsed time | P0 | US-BLD-5 |
| F-BLD-12 | Concurrency policy per task: `queue` (default for build), `restart` (cancel previous), `parallel` | P1 | - |
| F-BLD-13 | Tasks are server-side: closing the browser does not stop them; their output and result are in the ring buffer and history | P0 | US-SRV-1 |
| F-BLD-14 | Task history (last N runs per task with exit code, duration, output link) | P1 | - |
| F-BLD-15 | AI-invoked runs are identical to human ones and appear in the same output sources, tagged with the chat that started them | P0 | US-TOOL-3 |

## Output window

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-BLD-20 | Output tool window with a **dropdown** of sources ("Show output from:"), not tabs: Build, Run, Debug, Test, IED, Plugins, LSP: <name>, plus contributed sources | P0 | US-BLD-3 |
| F-BLD-21 | ANSI rendering (SGR colors mapped to theme tokens, bold, underline, cursor-less), 256-color and truecolor, hyperlinks (OSC 8) | P0 | US-BLD-3 |
| F-BLD-22 | Clickable locations: problem-matcher hits and generic `path(line,col)` / `path:line:col` patterns jump to the file (double-click, VS) | P0 | US-BLD-4 |
| F-BLD-23 | Controls: clear, word wrap, scroll lock, find in output, copy, "open as document" | P0 | US-BLD-3 |
| F-BLD-24 | Auto-show policy per source (VS: "show output window when build starts") | P0 | US-BLD-3 |
| F-BLD-25 | Virtualized rendering for very large outputs (100k+ lines) with server-side storage; client fetches windows | P1 | US-BLD-3 |

## Problems: matchers and Error List

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-BLD-30 | Error List tool window: Errors / Warnings / Messages toggles with counts, source filter (Build / IntelliSense = LSP / Spell / plugin), scope (open documents / current project / solution), columns (code, description, project, file, line), sort, search | P0 | US-BLD-4 |
| F-BLD-31 | Problem matchers (gen): regex-based (VS Code-compatible shape: file/line/col/severity/message/code groups, multi-line), contributed by plugins; the C4 plugin contributes one for `<relpath> (<line>:<col>) error: <msg>` | P0 | US-BLD-4 |
| F-BLD-32 | Path resolution for matcher hits: absolute, relative to step cwd, relative to a list of roots (project `root`, include dirs from LSP options) - needed because the C4 compiler prints paths relative to the owning source root | P0 | US-BLD-4 |
| F-BLD-33 | Structured diagnostics input: if a step writes a JSON diagnostics file (or JSON-lines on a side channel), use it instead of regexes (gen); request this from Nuke (`docs/17`) | P1 | US-BLD-4 |
| F-BLD-34 | Build diagnostics persist until the next build of that project (not cleared by LSP), and LSP diagnostics for the same location are de-duplicated | P0 | US-BLD-4 |
| F-BLD-35 | "Fix with AI" and "Explain" on Error List rows (single or multi-select) | P1 | US-INL-2 |

## Test Explorer

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-BLD-40 | Test discovery providers (gen): from a JSON results file (C4's `-test-json`), from a `list` command, from LSP CodeLens/document symbols tagged as tests | P1 | US-BLD-6 |
| F-BLD-41 | Test Explorer tool window: tree by project/file/test, status icons, durations, filter (failed, not run), run all / run failed / run selected / debug selected, playlists P2 | P1 | US-BLD-6 |
| F-BLD-42 | Failure details: message, expected/actual, location link, captured output; jump to the assert | P1 | US-BLD-6 |
| F-BLD-43 | CodeLens "Run | Debug" above test functions (via the language plugin) | P1 | US-BLD-7 |
| F-BLD-44 | Text-output fallback: a matcher for line-oriented test output (`[TEST]` / `ok` / `FAILED` / `FAIL:` for C4) so the Explorer works before Nuke passes `-test-json` | P1 | US-BLD-6 |
| F-BLD-45 | Coverage display (gutter green/amber/red) from lcov/LLVM coverage files when a provider produces them | P2 | - |

## Build insights

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-BLD-50 | Build-insights view for Chrome-trace build traces (Nuke writes `build-trace.json`): opens in the profiler's timeline | P2 | US-BLD-8, US-PRF-7 |
| F-BLD-51 | Per-project build time history chart (from `build-info.json`-style files) | P2 | US-BLD-8 |
