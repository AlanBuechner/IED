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
