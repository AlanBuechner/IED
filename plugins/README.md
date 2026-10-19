# plugins/ — default IED plugins

One folder per plugin; each is an ordinary project (built by Nuke) with a manifest, entry point and its own `skills/`. Full feature breakdowns: `../docs/05-default-plugins.md`. Manifest sketch + contribution model: `../docs/02-plugin-architecture.md`.

| Plugin | Role |
|---|---|
| `chat/` | AI chat: browser-like tabs, streaming thinking/tool calls, model groups, context panel; owns the default tool-call set (file/shell/web) |
| `editor/` | The IDE: Monaco + LSP/DAP, project tree, toolbar with command templates, output window, search, splits, markdown preview, inline AI edits, minimap, line indicators |
| `git/` | Fork/GitKraken-class source control UI (graph, staging, sync, conflicts) |
| `terminal/` | Always-present collapsible bottom shell (PowerShell/bash); also the backend for toolbar commands and the AI shell tool |
| `sandbox/` | Docker cross-OS build/test/develop; exposes autonomous "verify on linux" tools to the AI |
| `profiler/` | Tracy-class profiler: CPU/allocation views, dynamic thread rename + by-task grouping, GPU traces |

Sidebar order (default): chat, editor, git — then plugin-contributed icons. All six are disable-able; app must stay usable with just editor + terminal.
