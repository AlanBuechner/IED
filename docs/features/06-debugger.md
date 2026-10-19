# Features: debugger (DBG)

Owner: `src/dap` (host DAP client pool, session state) + `plugins/editor` (UI). The plan says "DAP support itself is built in": the host speaks DAP generically; language plugins contribute adapters (`dapProviders`) and launch-config templates. Every window is capability-gated - if the adapter does not support it, the window explains why instead of failing.

C4DAP today supports: launch, line breakpoints, exception filters (thrown/uncaught), continue/step/pause, threads, stack, one Locals scope, variables, limited evaluate. Most items below are P1 because they need adapter work as well (`docs/17`).

## Sessions

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-DBG-1 | DAP client pool: adapters over stdio (and TCP), one session per launch, multiple concurrent sessions, child sessions (`startDebugging` reverse request) | P0 | US-DBG-1 |
| F-DBG-2 | Launch configurations: contributed templates + per-project configs in `.ied.proj` (`debug` section), selected by the toolbar's debug target dropdown; machine-specific adapter paths in app tier | P0 | US-DBG-1 |
| F-DBG-3 | Pre-launch task (build) via the task system: F5 = build task -> on success -> debug step | P0 | US-DBG-1 |
| F-DBG-4 | Server-owned sessions: survive browser close; any client can attach to the session view; controls are revision-checked (two devices pressing "step" do not double-step) | P0 (survive), P1 (multi-device control) | US-DBG-4 |
| F-DBG-5 | Debug toolbar (VS): Continue (F5), Break All (Ctrl+Alt+Break), Stop (Shift+F5), Restart (Ctrl+Shift+F5), Show Next Statement (Alt+Num*), Step Into (F11), Step Over (F10), Step Out (Shift+F11) | P0 | US-DBG-1 |
| F-DBG-6 | Debug output: adapter `output` events and debuggee stdout/stderr to the *Debug* output source (ANSI kept) | P0 | US-DBG-1 |
| F-DBG-7 | Attach to process (Ctrl+Alt+P): process list with filter, adapter chooser | P1 | US-DBG-3 |
| F-DBG-8 | Staged adapter launch (copy the adapter binary to a cache and run the copy, so the adapter can be rebuilt while in use; restart when the source changes) (gen) - needed when working on C4DAP itself | P1 | - |

## Breakpoints

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-DBG-10 | Line breakpoints (F9, click glyph margin), enable/disable (Ctrl+F9), delete all (Ctrl+Shift+F9); verified/unverified/moved states shown | P0 | US-DBG-1 |
| F-DBG-11 | Breakpoints tool window: list, enable toggles, conditions, hit counts, labels, search, export/import | P1 | US-DBG-2 |
| F-DBG-12 | Conditional, hit-count, and tracepoint (log message, continue) breakpoints - capability-gated | P1 | US-DBG-3 |
| F-DBG-13 | Function, data (watch memory), and instruction breakpoints - capability-gated | P1 | US-DBG-3 |
| F-DBG-14 | Exception settings window: adapter exception filters with options | P0 | US-DBG-2 |
| F-DBG-15 | Breakpoints are personal workspace state (`.ied/local/`), persisted, not committed | P0 | - |

## Inspection windows

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-DBG-20 | Locals and Autos (Autos = variables on the current and previous line, derived from scopes when the adapter has no Autos scope) | P0 | US-DBG-2 |
| F-DBG-21 | Watch 1-4: add expression, edit, delete, drag from editor; values update on each stop; changed values highlighted | P0 | US-DBG-2 |
| F-DBG-22 | DataTips: hover a variable to evaluate (`evaluate` with context `hover`), expandable, pinnable | P0 | US-DBG-2 |
| F-DBG-23 | Call Stack: frames with module/function/file/line, double-click switches frame (Locals follow), copy stack, "show external code" toggle | P0 | US-DBG-2 |
| F-DBG-24 | Threads window: id, name, location, current marker, switch thread, freeze/thaw if supported | P0 | US-DBG-2 |
| F-DBG-25 | Immediate window: evaluate expressions in the current frame (REPL context), history | P1 | US-DBG-2 |
| F-DBG-26 | Set value (setVariable / setExpression) from Locals/Watch - capability-gated | P1 | US-DBG-3 |
| F-DBG-27 | Modules window (loaded modules, symbol status) | P1 | US-DBG-2 |
| F-DBG-28 | Memory window (readMemory / writeMemory, hex + ASCII, address expressions) | P1 | US-DBG-3 |
| F-DBG-29 | Disassembly window (disassemble), step by instruction, source interleaved | P1 | US-DBG-3 |
| F-DBG-30 | Registers window (via a registers scope or custom request) | P2 | US-DBG-3 |
| F-DBG-31 | Parallel Stacks / Parallel Watch (VS) | P2 | - |

## Execution control

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-DBG-40 | Run to cursor (Ctrl+F10), Set next statement (Ctrl+Shift+F10, `goto`), Step into specific (when `stepInTargets`) | P1 | US-DBG-3 |
| F-DBG-41 | Break on stop reasons: exception details popup (VS Exception Helper) with type, message, and "break when thrown" toggle | P1 | US-DBG-2 |
| F-DBG-42 | Reverse execution (stepBack / reverseContinue) - capability-gated | P2 | - |

## AI and profiler integration

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-DBG-50 | Debugger tools for the AI (T-DBG-*): start, breakpoints, continue/step, stack, scopes, evaluate, stop - over the same server session the UI shows | P1 | US-TOOL-4 |
| F-DBG-51 | "Ask AI about this stop": attaches stack + locals + source excerpt to a chat | P1 | US-INL-2 |
| F-DBG-52 | Diagnostic Tools pane while debugging: CPU and memory graphs and events, fed by the profiler plugin when the program is instrumented, else by process counters | P2 | US-DBG-5 |
