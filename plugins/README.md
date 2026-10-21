# plugins/ - built-in IED plugins

One folder per built-in plugin: its `ied-plugin.json` manifest, UI modules, skills, and (once the compiler is ready) its C4 sources compiled into the server (`runtime.kind = "builtin"`; C4 has no DLL output - `docs/02`). Overview: `docs/05`. Feature lists: `docs/features/`.

| Plugin | Role |
|---|---|
| `chat/` | AI chat (tabs, streaming, approvals, scratchpad, tasks, context panel), **Mission Control** and the goal board, default AI tool bindings |
| `editor/` | The Visual Studio-style IDE: Monaco on server buffers, LSP/DAP UI, Solution Explorer, toolbar tasks, Output, Error List, Find, Test Explorer, debugger windows, viewers, inline AI |
| `git/` | Fork/GitKraken-class source control on the git CLI |
| `terminal/` | Always-present bottom terminal (ConPTY), AI terminals per chat |
| `sandbox/` | Docker / WSL / SSH sandboxes with egress control and AI tools |
| `profiler/` | Tracy-class profiler on the ITP trace protocol (Tracy is a feature reference only) |

Default sidebar order: chat, editor, git, then plugin activities. All six are disable-able; IED must stay usable with only editor + terminal.

**As built:** `editor/` has its manifest and UI module (`ui/index.js`): the Editor activity (a dock host), Solution Explorer (solution groups and projects, or the folder; live on `tree.changed`), the Output window (sources, ANSI, follow, clear, wrap), and server documents in Monaco (`doc:<path>` tabs, M2): edits through `shared/ui-kit/docsync.js`, the lease with Edit Here, unsaved dots, Save / Save All (Ctrl+S, Ctrl+Shift+S), a status bar item (Ln, Col, indentation, line endings, language), the theme made from the tokens; a read-only view when Monaco is not installed. LSP and the rest follow in M2. `languages/` is declarative: ids, extensions, comment tokens and brackets of common languages (Monaco's ids, so its tokenizers color them), from which the server also takes a document's language. `git/` (M5, mockup 05): the Git activity on its own server code (`git/code/`, compiled into the server on the plugin API, `docs/26`) - the refs pane, the commit graph with lanes, a commit's details and diffs (inline or side by side), local changes staged by file, hunk or line, the commit composer, branches, stashes, fetch / pull / push with their progress, and the status bar's branch. `profiler/` (M9, mockup 06, `docs/16` "As built"): its server code in `code/` (the IED Trace Protocol listener, live captures streamed to `.ied/local/captures`, the queries behind the timeline and the analysis windows, Chrome trace and pprof import, the `profile_*` AI tools) and the Profiler activity in `ui/` (Captures; each capture a document with the frame graph, the timeline by thread or task, zone details, Statistics, Find Zone, Messages, Memory, Flame Graph, Ask AI). The other folders get their manifests when their server sides exist.
