# 25 - Debug adapters

## Purpose

How IED debugs (F-DBG-1..15, `docs/features/06-debugger.md`, `src/dap`): the server is the DAP client, so a debug session belongs to the server, not to a browser tab - it survives the tab closing, any device can watch it and drive it, and an AI tool steps the same session a person sees. Everything about a debugger comes from a plugin's `debugAdapters` contribution; the host knows no language and no debugger. The first adapter is C4DAP through the `c4` plugin (`C4/docs/tools/03-editor-lsp-dap.md` section 3); C/C++ follows through a C/C++ adapter (lldb-dap, or cppvsdbg-compatible ones) in the `ied.cpp` plugin.

## Adapters

A plugin contributes adapters:

```jsonc
"debugAdapters": [{
	"type": "c4",                                        // what a launch configuration names
	"label": "C4",
	"command": "${pluginDir}/../tools/C4DAP/C4DAP.exe",  // the variables of docs/24 "Profiles and selection", ${config:...} too
	"args": [], "env": {}, "cwd": "${workspaceDir}",
	"launchMode": "staged",                              // as for language servers: run a copy, so the adapter can be rebuilt
	"languages": ["c4"],                                 // where breakpoints may be set for it
	"when": "workspaceTrusted",
	"configurations": [                                  // templates offered when a project has none
		{ "name": "Launch", "request": "launch", "program": "${projectOutput}", "stopOnEntry": false }
	]
}]
```

An adapter runs over stdio, one process per session (DAP's model). The last loaded plugin's adapter of a `type` wins, like profiles. An untrusted workspace starts no adapter at all (`docs/11`): debugging runs the project's code.

## Launch configurations

A project's configurations live in its `.ied.proj` (`docs/03`) as `debug: [{name, type, request, preLaunchTask?, ...adapter arguments}]`; `nuke init --ied` writes one per executable project (`type` from the workspace's toolchain plugin, `program` the executable Nuke builds). The toolbar's debug target is a project and one of its configurations; without one, the adapter's first template for the project's languages.

A configuration's strings expand the profile variables plus `${projectName}`, `${projectDir}`, `${projectOutput}` (the build system's output for the active configuration, `docs/06`) and `${configuration}`. `preLaunchTask` names a task (`docs/06`); F5 runs it first and starts the session only when it succeeds (F-DBG-3). Machine-specific adapter paths belong in settings (`${config:...}`), never in the committed project file.

## Sessions

`dbg.start {wsId, project?, configuration?}` answers `{sessionId}` at once; the session then goes through its states, published on `dbg:<wsId>`:

`building` (the pre-launch task) -> `starting` (the adapter's `initialize`, the workspace's breakpoints and exception filters, `launch` or `attach`, `configurationDone`) -> `running` <-> `stopped` -> `ended` (with the exit code), or `failed` (with why: the task failed, the adapter would not start, the launch was refused).

The host speaks for the person: `initialize` says `clientID: "ied"`, lines and columns from 1, paths as paths, and what IED supports of the adapter's reverse requests (`runInTerminal` through the terminal service, `startDebugging` as a child session). Several sessions run at once, each its own adapter process; the debuggee and the adapter are bound to the server's job object, so they never outlive it.

| Topic | Events | Snapshot |
|---|---|---|
| `dbg:<wsId>` | `dbg.session` (a session's state changed) | every session: id, name, adapter type, state, revision, stop (thread, reason, description), exit code |
| `dbg:<wsId>:<sessionId>` | `dbg.stopped`, `dbg.continued`, `dbg.threads`, `dbg.breakpoint`, `dbg.output` | the session's state, threads, the stop's top frames, and the last 2000 output lines |

Output (`output` events, the debuggee's stdout and stderr) also goes to the workspace's `Debug` output source (F-DBG-6).

**Controls** are revision-checked (F-DBG-4): a session's `revision` grows every time it stops or runs on; `dbg.continue / next / stepIn / stepOut / pause {sessionId, threadId, revision}` from a revision that is not the current one answers `conflict`, so two devices pressing Step at once step once. `dbg.stop {sessionId}` terminates (or disconnects from an attached program), `dbg.restart` stops and starts the same configuration.

**Inspection** asks the adapter on the client's behalf, valid for the current stop: `dbg.stackTrace {sessionId, threadId}`, `dbg.scopes {sessionId, frameId}`, `dbg.variables {sessionId, variablesReference}`, `dbg.evaluate {sessionId, frameId?, expression, context}` (`watch`, `hover`, `repl`), and `dbg.request {sessionId, command, arguments}` for what an adapter's capabilities add (memory, disassembly, modules, setVariable...). A frame's source comes back as a workspace path, or as a `file:` URI for a file outside the workspace, recorded as named (`docs/10` "Files outside the workspace") so it opens read-only. Lines and columns are 0-based for clients like every other position (`docs/10`), the host adding or removing the 1.

## Breakpoints

Breakpoints are the workspace's, not a session's: a person sets them before any session, every session gets them, and they persist per machine in `.ied/local/breakpoints.json` (F-DBG-15). `bp.set {wsId, path, breakpoints: [{line, enabled, condition?, hitCondition?, logMessage?}]}` replaces a file's; `bp.list {wsId}`; topic `bp:<wsId>` with each file's breakpoints and, per running session, whether each is verified and where it bound (an adapter moving a breakpoint to the next line with code is shown, F-DBG-10). A conditional, hit-count or log breakpoint goes only to adapters whose capabilities take it; the others report it unverified with the reason. Edits move breakpoints with their lines (a `doc.edit` above a breakpoint shifts it), as the editor's decorations do.

Exception filters (F-DBG-14) are per workspace and adapter type, in `.ied/local/exceptions.json`, from the filters the adapter's `initialize` offers.

## What the host does not do

It never interprets a debugger's data beyond paths and positions: values, types and expressions are the adapter's. It does not build: the pre-launch task is the task system's, which invokes the build system (`docs/06`). It does not decide where a program's console goes: an adapter asks for `runInTerminal` when it wants one.

## Testing

`tools/server-checks/fake_dap.py` (an adapter that logs what it gets, stops at its "breakpoints", and exits on request) drives the pool in server checks: states, revision conflicts, breakpoints and their verification, paths and positions both ways, output, a session surviving its client, a crash of the adapter. C4DAP is exercised live on a game workspace (F5 on a C4 program: break, step, Locals, Call Stack). The mock server gets `dbg.*` and `bp.*` with a scripted session for the web client's e2e scenarios.
