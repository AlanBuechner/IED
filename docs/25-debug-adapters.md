# 25 - Debug adapters

## Purpose

How IED debugs (F-DBG-1..15, `docs/features/06-debugger.md`, `src/dap`): the server is the DAP client, so a debug session belongs to the server, not to a browser tab - it survives the tab closing, any device can watch it and drive it, and an AI tool steps the same session a person sees. Everything about a debugger comes from a plugin's `dapProviders` contribution (`shared/host-api/schemas/plugin-manifest.v1.schema.json`); the host knows no language and no debugger. The first adapter is C4DAP through the `c4` plugin (`C4/docs/tools/03-editor-lsp-dap.md` section 3); C/C++ follows through a C/C++ adapter (lldb-dap, or cppvsdbg-compatible ones) in the `ied.cpp` plugin.

## Adapters

A plugin contributes adapters:

```jsonc
"dapProviders": [{
	"type": "c4",                                        // what a launch configuration names
	"label": "C4",
	"command": "${pluginDir}/../tools/C4DAP/C4DAP.exe",  // the variables of docs/24 "Profiles and selection", ${config:...} too
	"args": [], "env": {}, "cwd": "${workspaceDir}",
	"transport": "stdio",
	"launchMode": "staged",                              // as for language servers: run a copy, so the adapter can be rebuilt
	"languages": ["c4"],                                 // where breakpoints may be set for it
	"when": "workspaceTrusted",
	"configurationTemplates": [                          // offered when a project has no configuration
		{ "name": "Launch", "request": "launch", "program": "${projectOutput}", "stopOnEntry": false }
	]
}]
```

An adapter runs over stdio, or with `transport: tcp` over a loopback socket: IED listens on a port of its own choosing, puts it in the adapter's `args` as `${port}` and waits for the adapter to connect (CodeLLDB's `--connect ${port}`); the adapter's output is then only a log. One process per session (DAP's model). The last loaded plugin's adapter of a `type` wins, like profiles. An untrusted workspace starts no adapter at all (`docs/11`): debugging runs the project's code.

## Launch configurations

A project's configurations live in its `.iedproj` (`docs/03`) as `debug: {default, configurations: {<name>: {type, request, preLaunchTask?, ...adapter arguments}}}` (a preset's `debug` too, overridden by name). A project `nuke init --ied` describes gets its configuration from its language's Nuke preset (`c4.nuke`: a C4DAP Launch of the executable Nuke builds; `cpp.nuke`: an LLDB one; `docs/26` section 6); the project file itself holds none unless someone adds one. The toolbar's debug target is a project and one of its configurations; without one, the adapter's first template for the project's languages.

A configuration's strings expand the profile variables plus `${projectName}`, `${projectDir}`, `${projectOutput}` (the program the build leaves for the active configuration: the project file's `buildSystem.output` with its variables filled in - `nuke init --ied` writes it, `docs/26` section 6) and `${configuration}`. `preLaunchTask` names a task (`docs/06`); F5 runs it first and starts the session only when it succeeds (F-DBG-3). Machine-specific adapter paths belong in settings (`${config:...}`), never in the committed project file.

## Sessions

`dbg.start {wsId, project?, configuration?}` answers `{sessionId, topic, output}` at once (`output`: the source to show - the pre-launch task's while it builds, else `debug`); the session then goes through its states, published on `dbg:<wsId>`:

`building` (the pre-launch task) -> `starting` (the adapter's `initialize`, the workspace's breakpoints and exception filters, `launch` or `attach`, `configurationDone`) -> `running` <-> `stopped` -> `ended` (with the exit code), or `failed` (with why: the task failed, the adapter would not start, the launch was refused).

The host speaks for the person: `initialize` says `clientID: "ied"`, lines and columns from 1, paths as paths, and what IED supports of the adapter's reverse requests (`runInTerminal` through the terminal service, `startDebugging` as a child session). Several sessions run at once, each its own adapter process; the debuggee and the adapter are bound to the server's job object, so they never outlive it.

| Topic | Events | Snapshot |
|---|---|---|
| `dbg:<wsId>` | `dbg.session` (a session's state changed) | every session: id, name, adapter type, state, revision, stop (thread, reason, description), exit code |
| `dbg:<wsId>:<sessionId>` | `dbg.stopped`, `dbg.continued`, `dbg.threads`, `dbg.breakpoint`, `dbg.output`, `dbg.usage` | the session's state, threads, the stop's top frames, the last 2000 output lines, and what the program has used |

Output (`output` events, the debuggee's stdout and stderr) also goes to the workspace's `Debug` output source (F-DBG-6).

**Controls** are revision-checked (F-DBG-4): a session's `revision` grows every time it stops or runs on; `dbg.continue / next / stepIn / stepOut / pause {sessionId, threadId, revision}` from a revision that is not the current one answers `conflict`, so two devices pressing Step at once step once. `dbg.stop {sessionId}` terminates (or disconnects from an attached program), `dbg.restart` stops and starts the same configuration.

**Inspection** asks the adapter on the client's behalf, valid for the current stop: `dbg.stackTrace {sessionId, threadId}`, `dbg.scopes {sessionId, frameId}`, `dbg.variables {sessionId, variablesReference}`, `dbg.evaluate {sessionId, frameId?, expression, context}` (`watch`, `hover`, `repl`), and `dbg.request {sessionId, command, arguments}` for what an adapter's capabilities add (memory, disassembly, modules, setVariable...). A frame's source comes back as a workspace path, or as a `file:` URI for a file outside the workspace, recorded as named (`docs/10` "Files outside the workspace") so it opens read-only. Lines and columns are 0-based for clients like every other position (`docs/10`), the host adding or removing the 1.

## Breakpoints

Breakpoints are the workspace's, not a session's: a person sets them before any session, every session gets them, and they persist per machine in `.ied/local/breakpoints.json` (F-DBG-15). `bp.set {wsId, path, breakpoints: [{line, enabled, condition?, hitCondition?, logMessage?}]}` replaces a file's; `bp.list {wsId}`; topic `bp:<wsId>` with each file's breakpoints and, per running session, whether each is verified and where it bound (an adapter moving a breakpoint to the next line with code is shown, F-DBG-10). A conditional, hit-count or log breakpoint goes only to adapters whose capabilities take it; the others report it unverified with the reason. Edits move breakpoints with their lines (a `doc.edit` above a breakpoint shifts it), as the editor's decorations do.

Exception filters (F-DBG-14) are per workspace and adapter type, in `.ied/local/exceptions.json`, from the filters the adapter's `initialize` offers.

## The server as built (M3)

`src/dap/`: `providers` (the `dapProviders` contributions; the last loaded plugin's adapter of a type, its `when` checked), `session` (one adapter process: a writer thread, readers for its messages and its errors, requests answered by sequence number, the state and revision), `breakpoints` (the workspace's, `bp.*`) and `service` (the `Debugger`: `dbg.*`, the topics, paths and positions).

- `dbg.start {wsId, project?, configuration?}` resolves the configuration through the task system (`docs/06`: the project's `debug` over its presets', strings expanded with the task variables), starts its `preLaunchTask` with `task.run`'s rules and the adapter when it succeeds, and answers `{sessionId, topic, output}`. `dbg.stop` while it builds ends the session and cancels the build. An untrusted workspace answers `forbidden`; no configuration, or no adapter of its `type`, `not_found`.
- The adapter gets `initialize`, then `launch` / `attach` with the configuration's object; its `initialized` event gets every file's breakpoints (enabled ones; a condition, hit count or log message the adapter's capabilities do not take is not sent and reported unverified with the reason), `setExceptionBreakpoints` with the filters the adapter marks `default`, and `configurationDone`.
- Topics: `dbg:<wsId>` (snapshot `{sessions: [summary]}`, event `dbg.session` with a summary: `{id, name, project, type, state, revision, problem, exitCode, stop: {threadId, reason, description} | null}`) and `dbg:<wsId>:<sessionId>` (snapshot `{session, threads, frames, output: [text]}`; events `dbg.state` (a summary), `dbg.stopped` (`{session, threads, frames}`: the stopped thread's top 20 frames, fetched before the stop is published), `dbg.output` (`{category, text}`, also appended to the `debug` output source), `dbg.usage` ("Diagnostic Tools" below)). `dbg.sessions {wsId}` answers the workspace snapshot. The last ten ended sessions are kept.
- A frame is `{id, name, line, column, presentationHint?, source: {name, path | uri, sourceReference?}}`, lines and columns from 0.
- Controls answer `{revision}`: continue and the steps move the session to `running` at once (the revision grows), so a second press from the same revision is a `conflict`.
- `bp:<wsId>`: snapshot `{files: [{path, breakpoints}], bound: [{sessionId, path, breakpoints: [{line, verified, boundLine, message?}]}], watches: [expression]}`; events `bp.changed` (a file's breakpoints), `bp.bound` (a session's binding of a file), `bp.unbound` (`{sessionId}`, the session ended), `watch.changed` (`{expressions}`). `bp.set` answers the file's breakpoints; `bp.list {wsId}` the snapshot; `watch.set {wsId, expressions}` replaces the Watch window's expressions (kept with the breakpoints, so they come back on every device).
- Not yet: breakpoints following edits, choosing exception filters (Exception Settings), `runInTerminal` and `startDebugging` (refused, so an adapter does without), and the adapter's templates for a project without configurations.
- C and C++ (and Rust) debug through CodeLLDB, the `ied.cpp` plugin's `lldb` adapter (transport tcp; `cpp.lldb.path` names its `codelldb.exe`). Checked live: a C program built with the vendored clang, stopped at a breakpoint with its locals.

## The client as built (M3)

`plugins/editor/ui/debug.js` (`DebugState` follows `dbg:<wsId>` and the current session's topic - the newest live session, else the newest; `BreakpointState` follows `bp:<wsId>`), `breakpointmargin.js` (each editor's glyph margin, attached through `Editors.attach`), `debugviews.js` (Call Stack, Locals, Watch, Threads, Breakpoints), a hover provider per language that shows the value under the mouse while stopped (DataTips: the identifier with the member accesses before it, `dbg.evaluate` with context `hover`) and the commands, keys and menus in the editor plugin's manifest (`docs/15` "Debugging"). The context key `debugState` (`running`, `stopped`, unset) enables the run controls. The `c4` plugin contributes the `c4` adapter (C4DAP) and the `c4.nuke` preset's `Launch` configuration: the program Nuke builds for the selected configuration and platform, with `build` as its pre-launch task. Checked live on a Nuke workspace: a breakpoint, F5 building and stopping there, Locals with C4's types, F10, Shift+F5. Not yet: the Exception Settings window, editing a breakpoint's condition, breakpoints in files outside the workspace, Immediate, Modules, Memory and Disassembly (M8).

## Diagnostic Tools

VS's Diagnostic Tools window (F-DBG-52): what the debugged program uses over the session, beside the code while debugging.

- **Where the numbers come from.** A debug adapter names the program's process with DAP's `process` event (`systemProcessId`, `name`). From then on, while the session lives, a thread of the server reads that process every 500 ms with Bedrock's `Process::GetUsage` (`C4/docs/library/bedrock.md` section 15): its processor time, giving its share of all processors since the reading before, plus its private bytes and working set. The lock is held only to keep and publish a reading. It stops when the session ends or the process can no longer be read. C4DAP sends `process` when the program starts (`Engine::ProcessCreated`); lldb-dap, debugpy and others send it too. With an adapter that never does, the window says so.
- **On the wire.** `dbg.usage` on the session topic: `{processId, name, sample: [ms, cpu %, privateBytes, workingSetBytes]}`, where `ms` counts from when the process was named and `cpu` is the share of all processors with one decimal. The session snapshot's `usage` is `{processId, name, samples: [...]}` with the last 1200 readings (10 minutes), or null before the process is named. The readings stay with an ended session.
- **The window** (`ied.diagnosticTools`, `plugins/editor/ui/diagtools.js`):
  - two graphs over the session so far: Process Memory (MB), with private bytes filled and the working set as a line, and CPU (% of all processors);
  - each graph shows its current values and a round scale top;
  - a header names the process and the session's time.
- **Where it shows.** The debugging layout docks it on the right (`docs/15` "Window layouts"). Ctrl+Alt+F2 shows it while debugging (the profiler keeps the key otherwise), and View lists it.
- **Not yet:** the profiler's numbers for an instrumented program (F-PRF-63), events on the timeline (breaks, exceptions, GCs), and a selection of the timeline.

## What the host does not do

It never interprets a debugger's data beyond paths and positions: values, types and expressions are the adapter's. It does not build: the pre-launch task is the task system's, which invokes the build system (`docs/06`). It does not decide where a program's console goes: an adapter asks for `runInTerminal` when it wants one.

## Testing

`tools/server-checks/fake_dap.py` (an adapter that logs what it gets, stops at its "breakpoints", and exits on request) drives the pool in server checks: states, revision conflicts, breakpoints and their verification, paths and positions both ways, output, a session surviving its client, a crash of the adapter, and Diagnostic Tools' readings. For those it names its own process in a `process` event. C4DAP is exercised live on a game workspace (F5 on a C4 program: break, step, Locals, Call Stack). The mock server gets `dbg.*` and `bp.*` with a scripted session for the web client's e2e scenarios, and reports usage for it: busy while it runs, idle while stopped, its memory growing. C4DAP's session test checks its `process` event.
