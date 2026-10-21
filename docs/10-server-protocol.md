# 10 - Server protocol v1 (client <-> server)

## Purpose

Resolve `docs/99` #2: the wire contract between the web client and `ied-server`. It must carry AI token streams, build/terminal output floods, LSP diagnostics and editor edits without dropping anything, and make "close the browser, come back tomorrow" seamless. Everything here is generic: no topic, method or payload names anything language- or engine-specific.

Status: **design v1, ready for implementation.** Machine-readable frame schema: `shared/host-api/protocol/frames.v1.schema.json`.

## Transport

| Channel | Used for |
|---|---|
| `GET /` and static paths | the web app (HTML/JS/CSS, plugin UI modules under `/plugins/<id>/ui/...`, Monaco, xterm) |
| `GET /ui-kit/<path>`, `GET /codicons/<name>.svg`, `GET /vendor/<library>/<path>` | the components shared by the shell and plugin UI modules (`shared/ui-kit/`), one codicon each (`third_party/codicons`; an alias in its `mapping.json` names its icon, so `run` draws `play` as `$(run)` does in VS Code), and the install's web libraries (`vendor/web/`: Monaco, xterm.js), cacheable for a day |
| `POST /api/auth/*` | login, logout, token exchange (`docs/11`) |
| `GET /api/blob/<id>` | large binary payloads (images, trace chunks, file downloads), referenced from frames by id |
| `GET /w/<wsId>/files/<path>` | a workspace's image (PNG, JPEG, GIF, WebP, BMP, ICO) for previews (a Markdown document's pictures): never another type, so a repository's HTML or SVG cannot run script on IED's origin; sent with `X-Content-Type-Options: nosniff` and a sandboxing `Content-Security-Policy` |
| `PUT /api/upload` | attachments and file uploads (streamed, returns a blob id) |
| `GET /ws` | **one WebSocket per browser tab**, all real-time traffic |
| `/w/<ws>/port/<n>/...` | port-forward proxy (F-SRV-50) |

Rationale for one socket per tab (not per device): tabs can view different workspaces, and a tab is the unit that resumes. The server tracks a **client** = one socket identity (persisted id in `sessionStorage`) belonging to a **device** (persisted id in `localStorage` + auth session).

Frames are JSON text messages, UTF-8. Binary WebSocket messages are reserved for v1.1 (terminal and trace bulk data) - v1 keeps everything JSON to simplify debugging; terminal output is base64 only when it contains invalid UTF-8.

## Frame types

Every frame is an object with a `t` (type) field.

```jsonc
// client -> server
{ "t": "hello",   "proto": 1, "clientId": "c-...", "deviceId": "d-...", "deviceClass": "desktop|tablet|phone",
                   "resume": { "sessionEpoch": "e-...", "cursors": { "<topicKey>": 1234, ... } } }
{ "t": "req",     "id": 17, "method": "fs.read", "params": { ... } }
{ "t": "cancel",  "id": 17 }                                   // cancels an in-flight req (or a stream it opened)
{ "t": "sub",     "id": 18, "topics": ["chat:<chatId>", "task:<taskId>"], "from": { "chat:<chatId>": 1200 } }
{ "t": "unsub",   "topics": ["..."] }
{ "t": "ack",     "cursors": { "<topicKey>": 1290 } }          // optional: lets the server trim per-client state
{ "t": "ping",    "ts": 1729350000000 }

// server -> client
{ "t": "welcome", "proto": 1, "serverVersion": "0.1.0", "sessionEpoch": "e-...", "clientId": "c-...",
                   "resume": { "<topicKey>": "ok" | "snapshot" } }
{ "t": "res",     "id": 17, "ok": true,  "result": { ... } }
{ "t": "res",     "id": 17, "ok": false, "error": { "code": "conflict", "message": "...", "data": { ... } } }
{ "t": "evt",     "topic": "chat:<chatId>", "seq": 1291, "kind": "segment.delta", "data": { ... } }
{ "t": "snap",    "topic": "chat:<chatId>", "seq": 1291, "data": { ... } }  // full state of a topic at seq
{ "t": "gap",     "topic": "build:<runId>", "from": 800, "to": 950 }         // server could not deliver; client must re-snapshot
{ "t": "pong",    "ts": 1729350000000 }
{ "t": "bye",     "reason": "server-restart|auth-revoked|protocol-mismatch" }
```

- **Requests** (`req`/`res`) are for commands and queries. `id` is a client-chosen integer, unique per socket. Every request can be cancelled. Long operations return quickly with a handle (e.g. `{ "runId": ... }`) and report progress on a topic.
- **Events** (`evt`) are ordered per **topic** with a monotonically increasing `seq`. The client tracks the last `seq` it has applied per topic.
- **Snapshots** (`snap`) carry a topic's full current state as of `seq`; events after that `seq` apply on top.

## Topics

A topic is a stream of state changes for one server object. Naming: `<kind>:<id>`.

| Topic kind | Examples of event kinds | Snapshot contains |
|---|---|---|
| `server` | `workspace.opened`, `notification`, `device.connected` | open workspaces, notifications (unread), devices |
| `ws:<wsId>` | `tree.changed`, `trust.changed`, `solution.changed`, `plugins.changed` (its `.ied/plugins/` changed, a project plugin's code changed state, or code computed new contributions for it: read the contributions again, `docs/26`) | workspace summary (`kind` folder, file or solution), trust, the solution model (`docs/03`) |
| `settings:app`, `settings:<wsId>` | `settings.changed` | tier files (revision, problems) and every setting's effective value with its source (`docs/07`) |
| `plugins` | `plugins.changed` (also when a built-in or app plugin's code changes state) | every plugin's summary and state, with its code's (`docs/02`) |
| `layout:<wsId>:<deviceClass>` | `layout.changed {revision, layout, by}` | `{revision, layout}`: the layout document the client last stored for the workspace and device class (`docs/15`), verbatim; revision 0 with `layout: null` until one is. `layout.set {wsId, deviceClass, layout, expectedRevision}` stores one (an object up to 256 KB, or null to forget it) and answers `{revision}`; an old `expectedRevision` gets `conflict {revision}`. Kept in `.ied/local/layout/<deviceClass>.json` of a solution, and for a folder or a file in `<app data>/workspaces/<key>/layout/`, `key` the first 16 hex digits of the SHA-256 of the workspace folder's canonical path |
| `doc:<docId>` | `doc.edit`, `doc.saved`, `doc.lease`, `doc.diskChanged`, `doc.renamed` | text + version + lease holder + dirty |
| `diag:<wsId>` | `diag.set {path, source, items}` (LSP diagnostics, UTF-16 ranges; `docs/24`) | all current diagnostics, `{files: {<path>: {<source>: [diagnostic]}}}` |
| `chat:<chatId>` | `turn.start`, `segment.start`, `segment.delta`, `segment.end`, `tool.state`, `approval.request`, `tasks.changed`, `scratch.changed`, `turn.end`, `context.changed` | chat header + last N turns (older via `chat.history` request) |
| `run:<runId>` | `step.start`, `output` (source, text), `step.end`, `run.end` | task run state + output tail |
| `out:<wsId>:<sourceId>` | `append {text}` (coalescible), `clear` | `{sourceId, label, tail}`: the last 64 KB, cut at a line start. Every workspace has the `build`, `run`, `test` and `debug` sources of its tasks (`docs/06`), the `ai` source (the AI's `shell_run` commands and their output, `docs/13`), the `git` source (the git commands that change something, with what git said, "Git" below) and the `ied` source, the server's own log with each line stamped `[hh:mm:ss]`; `out.sources {wsId}` lists a workspace's sources and `out.clear {wsId, sourceId}` empties one |
| `terms:<wsId>` | `term.added`, `term.changed`, `term.removed` (a terminal's summary) | `{terminals: [summary]}`: the workspace's terminals, in the order they were made; a summary is `{termId, wsId, title, kind, alive, exitCode, holder}` |
| `term:<termId>` | `data {data}` (coalescible), `resize {cols, rows}`, `exit {code}`, `lease {holder}` | `{terminal: summary, cols, rows, scrollback}`: `scrollback` is the last 256 KB of output, replayed by a client to rebuild the screen |
| `dbg:<wsId>`, `dbg:<wsId>:<sessionId>`, `bp:<wsId>` | the debugger's (`docs/25` "The server as built") | the workspace's debug sessions, one session's stop, the breakpoints |
| `prof:<captureId>` | `stats`, `progress` | capture summary (timeline data is fetched via requests/blobs) |
| `git:<repoId>` | `status.changed`, `refs.changed`, `op.progress`, `op.end` | head, changes, refs and the running operation ("Git" below) |

Plugins add topics under their own prefix (`<pluginId>/<kind>:<id>`); the hub is generic.

**Files changed on disk.** The server watches each workspace folder, so changes made by anything else (a git checkout, another editor, a build) arrive like its own:

- `tree.changed {changes: [{path, change: created|changed|deleted}], overflow?: true}`. Changes are gathered until the folder has been quiet for 100 ms, at most a second, and folded per path: created then deleted is nothing, a rename is a delete and a create. `overflow` means the system dropped changes, so the client lists the folder again. A repository folder (`.git`, at any depth) is not reported.
- The server's own writes are reported once, by the method that made them; a change from outside moves the file's revision, so a write based on the old revision gets `conflict`.
- An open document whose file changes reloads when it is clean (a `doc.edit` with origin `disk`) and gets `doc.diskChanged {docId, revision}` when it is dirty. When its file is deleted it gets `doc.diskChanged` with revision 0 and keeps its text.
- `fs.rename` moves open documents with their files (a folder's too): the document keeps its docId, text, version and lease, and gets `doc.renamed {docId, path}`; clients show it under the new path (`doc.open` of the new path answers the same docId). `fs.delete` takes a folder only with `recursive: true`, and documents open under it get `doc.diskChanged` with revision 0.

## Resume

1. On connect the client sends `hello` with the `sessionEpoch` it last saw and its per-topic cursors.
2. If the server's `sessionEpoch` differs (server restarted), every topic resumes by **snapshot**: in-memory ring buffers are gone, but all durable state (chats, docs, layouts, run results) is reloaded from disk.
3. Otherwise, for each topic: if the ring buffer still holds `cursor+1`, the server replays events from there (`"ok"`); if not, it sends a `snap` (`"snapshot"`).
4. After resume, live events flow. The client never shows duplicate tokens: events with `seq <= cursor` are ignored.

**Ring buffers:** per topic, bounded by count and bytes (defaults: chat 20k events / 8 MB, run 50k lines / 16 MB, terminal 5 MB, others 2k events). Chat token deltas are **coalesced into persisted segments** at segment end, so an old chat never needs its deltas replayed - the snapshot holds completed segments, and only the in-flight segment's deltas live in the ring.

## Backpressure and floods

- The server keeps a per-client outbound queue with a byte budget (default 4 MB). Producers never block on a slow client.
- When a client's queue for a **coalescible** topic (`out:*`, `run:*`, `term:*`, `chat:*`) passes a threshold, the server merges pending growing-text events (`append`, `data`, a segment's `segment.delta`) into one larger event (fewer frames, same bytes).
- When the budget is exceeded, the server drops that client's pending events for the heaviest topic and sends a `gap`; the client re-subscribes with a snapshot request. Nothing is silently lost.
- Clients subscribe only to what is visible (plus the always-on `server`/`ws` topics); hidden panes unsubscribe after a grace period. Phones therefore never receive terminal floods for a collapsed drawer.

## Requests: conventions

- Method names: `<area>.<verb>` (`fs.read`, `doc.edit`, `chat.send`, `task.run`, `git.stage`). Plugin methods: `<pluginId>/<method>`.
- Params and results are JSON objects; schemas live in `shared/host-api/` and are versioned with the protocol.
- Error codes (string, stable): `bad_request`, `not_found`, `forbidden`, `untrusted`, `conflict` (revision mismatch - `data` carries the current revision), `busy`, `cancelled`, `timeout`, `unavailable`, `internal`, `plugin_error`.
- **Mutations carry the revision they were based on** (`baseVersion`, `expectedRevision`) wherever the target can be contended. The server rejects stale writes with `conflict`; the UI shows a resolve choice.
- **Idempotency:** mutations that may be retried after a reconnect carry a client-generated `opId`; the server remembers recent `opId`s per client for 10 minutes and returns the original result for duplicates (so a "send message" retried during a flaky connection is not sent twice).

## Files

Paths are workspace-relative with `/` separators. Every file has a `revision` (1 when the server first sees it, then one more per write, the server's or anyone else's).

| Method | Params | Result |
|---|---|---|
| `fs.list` | `wsId, path` | `{path, entries: [{name, path, kind: file\|dir, size?, revision?}]}`; a repository's `.git` folder (at any depth) is not listed, as it is not reported in `tree.changed` |
| `fs.read` | `wsId, path` | `{path, text, revision}` |
| `fs.write` | `wsId, path, text, expectedRevision?` (0: must not exist) | `{path, revision}`; folders are made |
| `fs.mkdir` | `wsId, path` | `{path}` |
| `fs.rename` | `wsId, from, to` (a file or a folder) | `{path, revision?}`; open documents follow (`doc.renamed`) |
| `fs.delete` | `wsId, path, expectedRevision?, recursive?` (a folder needs `recursive`) | `{trashId}`: the file or folder is moved to the trash |
| `fs.search` | `wsId, query, regex?, caseSensitive?, include?, exclude?, useIgnoreFiles?, maxResults?` | `{matches: [{path, line, col, preview}], truncated}`: the first match of each line, `col` in UTF-16 units, `preview` the line trimmed to 200 characters; `include` / `exclude` are globs on the path (without `exclude`: `.git`, `node_modules`, `build`, `bin`, `bin-int`, `toolchains`, `vendor`, `third_party`, `__pycache__`); what `.gitignore` files ignore is skipped unless `useIgnoreFiles` is false; binary files (a NUL in the first 8000 bytes) and files over 4 MB are skipped |
| `fs.files` | `wsId, query, maxResults?` (50; at most 500) | `{files: [{path, score}]}`: the files whose paths hold the query's characters in order (any case), best first - a match in the file's own name, runs and word starts score higher, shorter paths win ties; skips what `fs.search` skips |
| `trash.list` | `wsId?` (only that workspace's) | `{entries: [{id, root, path, kind, deletedMs, bytes}], bytes}`, newest first |
| `trash.restore` | `id` | `{wsId?, path}`: moved back where it was (`conflict` when something is there now) |
| `trash.purge` | `id` or `all: true` | `{}`: deleted for good |

**Files outside the workspace** are shown read-only: a library's or a toolchain's sources, system headers, the standard library of a language - wherever go to definition, references, Go To All or a debugger's stack leads. `doc.open {wsId, uri}` with a `file:` URI opens one as a document like any other (`{docId, version, holder: null, readOnly: true}`; its snapshot has `readOnly: true` and the URI as `path`), on the language server that named it, so its own hover and navigation work. Only a file a process of the workspace named can be opened this way: the host keeps, per workspace, every `file:` URI outside the workspace that a language server or debug adapter put in a result it passed to a client (`forbidden` for any other; `docs/11`). Such a document takes no lease and no edit (`doc.edit` answers `forbidden` with reason `read_only`), is never saved, and is not watched: opening it again after it was closed reads it again.

**Trash** (F-OPS-13): nothing a person or an AI deletes through the server is lost at once. A deleted file or folder moves to `<data>/trash/<id>/` (the server's data folder, `%LOCALAPPDATA%/IED/` by default) with an `entry.json` saying where it came from; `trash.restore` puts it back, `trash.purge` removes it. When the trash holds more than 4 GB the oldest entries are purged. Watchers see a trash move as a delete and a restore as a create, like any change.

## Terminals

A terminal is a shell (or any program) under a pseudo console on the server (ConPTY), so it outlives the browser: closing every tab leaves it running, and any device reopens it with its screen rebuilt from the scrollback.

| Method | Params | Answer |
|---|---|---|
| `term.create` | `wsId, cols?, rows?, title?, cwd?` (a workspace path), `argv?` (default: the `terminal.shell` setting, else PowerShell 7, Windows PowerShell or `cmd`) | the summary; the caller holds the lease |
| `term.list` | `wsId` | `{terminals: [summary]}` |
| `term.input` | `termId, data` | `{}`; only the lease holder types (`forbidden` with reason `lease` and the holder otherwise) |
| `term.takeLease` | `termId` | `{holder}`: this client types from now on (the others see the `lease` event) |
| `term.resize` | `termId, cols, rows` | `{}`; the lease holder's size wins (a client that does not hold it is ignored) |
| `term.rename` | `termId, title` | the summary |
| `term.close` | `termId` | `{}`: the program's tree is ended; the terminal leaves the list |

A terminal's program runs in the workspace folder (or `cwd`) with the server's environment and `TERM_PROGRAM=IED`. When it exits the terminal stays in the list, `alive: false` with its `exitCode`, until closed. An untrusted workspace still gets terminals: the person typing is the one deciding what runs (`docs/11`).

## Document editing protocol

Server-owned buffers (F-EDT-1) with a simple, sufficient scheme:

- Each `doc` has an integer `version`. The **lease holder** sends `doc.edit { docId, baseVersion, changes: [ {range, text} ... ], opId }`.
- If `baseVersion == version`, the server applies, increments, and broadcasts `doc.edit` to all subscribers (including the sender, who uses it as the ack).
- A document is **dirty** while its text differs from its file's as last read, saved, or seen changed on disk: every `doc.edit` event carries `dirty`, so undoing back to the saved text makes it clean again (as VS does). Clients take it when none of their own edits are still on the way.
- `doc.baseline {docId}` answers `{version, opened, saved}`: the text when the server opened the document and the file's text as last read, saved or seen changed on disk. The track-changes margin marks lines that differ from `saved` (unsaved, yellow) and those that differ only from `opened` (saved since, green); a client asks again after `doc.saved` and after an edit with origin `disk`.
- If not (an AI edit or file reload slipped in), the server **rebases** the client's changes over the intervening changes (operational transform on line/column ranges - only one concurrent writer class at a time makes this cheap), applies, and broadcasts. If rebase is ambiguous (overlapping ranges), it returns `conflict` and the client re-syncs.
- Non-holders are read-only replicas. `doc.takeLease` transfers the lease (the previous holder's pending edits are flushed first).
- Server-side writers (AI tools, refactors, replace-in-files, file reload) apply as the server itself and are broadcast like any edit, tagged with `origin` (`ai:<chatId>`, `lsp`, `disk`, `user:<clientId>`) for margin coloring and undo grouping.

Language servers receive changes from the server's document store (the server is the LSP client), so every device sees the same diagnostics.

**The client side as built** (`shared/ui-kit/docsync.js`, unit-tested against a model of the server with seeded random interleavings):

- The holder sends one `doc.edit` at a time, so the server applies its edits in order; edits made meanwhile wait in a queue. An edit is acknowledged by its own `doc.edit` event (matched by `opId`), never by the response: events come in order, so no other writer's edit can be passed over.
- Another writer's edit arrives in the server's coordinates, before this client's pending edits. The client moves it over them with the mirror of the server's rule (where both insert at one point, the server's edit goes first; overlapping ranges cannot be placed) and applies it; the pending edits move over it with the server's rule. The edit in flight is rebased by the server, and its moved form stays the client's reference for the next remote edit.
- When an edit cannot be placed (overlap, `conflict`, a lost lease), the client subscribes again and starts over from the snapshot, passing over events until it arrives, and tells the person.
- The languageId of a snapshot comes from the loaded plugins' `languages` contributions (file names, then extensions; the plugin loaded last wins), else `plaintext`: the host knows no language itself.

## Chat streaming detail

```jsonc
{ "t":"evt","topic":"chat:42","seq":1001,"kind":"turn.start","data":{"turnId":"u7","mode":"agent","group":"plan-claude/impl-local","model":"qwen3.8-27b@spark","user":{"text":"Port the logger"},"startedMs":1792585000000} }
{ "t":"evt","topic":"chat:42","seq":1002,"kind":"segment.start","data":{"turnId":"u7","segId":"s1","type":"thinking","done":false,"model":"qwen3.8-27b@spark","text":""} }
{ "t":"evt","topic":"chat:42","seq":1003,"kind":"segment.delta","data":{"segId":"s1","text":"Let me look at"} }
{ "t":"evt","topic":"chat:42","seq":1040,"kind":"segment.end","data":{"segId":"s1","tokens":212,"ms":4100} }
{ "t":"evt","topic":"chat:42","seq":1041,"kind":"segment.start","data":{"turnId":"u7","segId":"s2","type":"tool","done":false,"tool":"fs_read","callId":"k1","argsText":"","state":"streaming"} }
{ "t":"evt","topic":"chat:42","seq":1042,"kind":"segment.delta","data":{"segId":"s2","argsText":"{\"path\":\"src/Ri"} }
{ "t":"evt","topic":"chat:42","seq":1050,"kind":"tool.state","data":{"callId":"k1","state":"running"} }
{ "t":"evt","topic":"chat:42","seq":1051,"kind":"tool.state","data":{"callId":"k1","state":"ok","ms":38,"source":"built-in","result":{"preview":"src/Ring.ch (120 lines)","isError":false}} }
{ "t":"evt","topic":"chat:42","seq":1099,"kind":"turn.end","data":{"turnId":"u7","stop":"finished","recoveries":[],"tokensIn":18233,"tokensOut":911,"ttftMs":1400,"genMs":23800,"endedMs":1792586000000} }
```

- Deltas go out as the model server streams them; a client that falls behind gets a segment's queued deltas merged into one (`chat:*` is coalescible, "Backpressure" above), so a phone on a slow link gets fewer, larger frames.
- `stop` is `finished`, `user_cancelled`, `no_model`, `interrupted` or the stop reason that paused the chat (`docs/12` taxonomy); the recoveries say what the harness did on the way ("Chats" below).

## Chats

A chat belongs to a workspace and to the server: a turn runs on the server until it ends, whatever the browsers do, and every device sees the same chat (`docs/12`). The runtime is `src/ai/` (`docs/04` "As built"); its model settings come from `ai.models` and `ai.groups`.

| Method | Params | Answer |
|---|---|---|
| `chat.list` | `wsId` | `{chats: [summary]}`: a summary is `{chatId, wsId, title, mode, permission, group, state, stateReason?, turnCount, updatedMs, tasksDone, tasksTotal, approvalsWaiting}`; `state` is `idle`, `running`, `awaiting_approval`, `awaiting_answer`, `paused` (`stateReason` says why: a stop reason, or `interrupted`); `group` is the chat's own choice, empty for the default |
| `chat.create` | `wsId, title?, mode?` (`ask`, `plan`, `agent`; default `agent`), `permission?` (`plan`, `ask`, `acceptEdits`, `auto`; default `plan` in Plan mode, else `ask`), `group?` | the summary. A chat titled "New chat" takes its first message's first line as its title |
| `chat.set` | `chatId`, any of `title, mode, permission, group` | the summary (a mode, permission or group change applies from the next step) |
| `chat.send` | `chatId, text, as?` (`message` default, `queue`, `inject`) | `{turnId}` when it starts a turn (from `idle` or `paused`); while a turn runs, `message` and `queue` hold it for the next turn (`{queued: n}`, `queue.changed`) and `inject` gives it to the running turn at its next step (`{injected: true}`, `docs/12` "Steering"). A turn that ends `finished` starts the first queued message as the next turn |
| `chat.unqueue` | `chatId, index` | `{queued}`: the queued message at `index` taken off the queue (to edit it, send it now or drop it) |
| `chat.stop` | `chatId` | `{state}`; the turn ends with `user_cancelled` (a running shell command is killed) |
| `chat.approve` | `chatId, approvalId, decision` (`allow`, `deny`, `chat`), `message?` (a denial's reason, given to the model) | `{decision}`. `chat` allows the call and adds the approval's `subject` rule to the chat's own allow rules ("Allow for this chat"). "Always allow" is the client's: it adds the rule to `ai.permissions.allow` with `settings.set` (app tier), then answers `allow`. `conflict {decision, by}` when another device answered first, `not_found` when the approval is gone |
| `chat.answer` | `chatId, questionId, answer` | `{}`: the `ask_user` question's answer; `conflict` when answered already |
| `chat.history` | `chatId, before?, limit?` (20) | `{turns, hasMore}`: the turns before `before` (a turnId; the newest without it) |
| `ai.probe` | `wsId, model` (an `ai.models` name) or `wsId, profile` (an entry not saved yet) | `{ok, error?, server, models, modelServed, contextWindow, trainContext, slots, ms}`: what the model's server says of itself - `GET <endpoint>/models` (the ids; vLLM's `max_model_len`, llama.cpp's `meta.n_ctx`) and `GET <root>/props` (llama.cpp: the slot context and the slots, `server` "llama.cpp"). A saved model's window is kept for its turns (`docs/12` "Truth about the model's context window") |
| `chat.delete` | `chatId` | `{}`; `conflict` while a turn runs. Its files go too (`.ied/local/chats/<chatId>/`) |

| Topic | Events | Snapshot |
|---|---|---|
| `chats:<wsId>` | `chat.created`, `chat.changed` (a summary), `chat.deleted {chatId}` | `{chats: [summary]}` |
| `chat:<chatId>` | `turn.start`, `segment.start`, `segment.delta`, `segment.end`, `tool.state`, `approval.request`, `approval.resolved`, `question.request`, `question.resolved`, `tasks.changed`, `queue.changed`, `files.changed`, `context.changed`, `turn.stats`, `turn.end`, `chat.changed` | `{...summary, model, groupInUse, tasks, queued, modified, context, hasMore, turns: [the last 20], pendingApprovals, pendingQuestion}`: `model` and `groupInUse` are what a turn would use now |

A turn is `{turnId, user: {text}, mode, model, group, segments, stop, recoveries, tokensIn, tokensOut, ttftMs, genMs, startedMs, endedMs}`: `stop` is null while it runs; `tokensIn` is the last request's prompt, `tokensOut` every request's completion, `ttftMs` the last time to first token and `genMs` the time spent generating after it (tokens per second = `tokensOut / genMs`).

A segment is `{segId, type, done, model?, tokens?}` plus by type:

- `text`, `thinking`: `text`. Thinking comes from the reasoning fields and `<think>` blocks (`docs/12` "Dialect normalization"); a tool call a model wrote as text never shows as prose.
- `tool`: `tool`, `callId`, `argsText` (streamed), and once known `state` (`streaming`, `awaiting_approval`, `awaiting_answer`, `running`, `ok`, `error`, `denied`, `interrupted`), `ms` (the run, without the wait for an approval), `plus` / `minus` (lines a write added and removed), `source` (`built-in`, `Output: AI`, `Output: Build`) and `result {preview, isError}`. A write's preview is its unified diff.
- `harness`: `text` and `kind` - a note IED gave the model (`continue_after_max_tokens`, `empty`, `agent_no_finish`, ...), or one it shows only (`paused`, `routing`, `no_model`): `docs/12` "Transparency".
- `steering`: `text`, a message a person injected.

Events:

| Event | Data |
|---|---|
| `turn.start` | `{turnId, mode, group, model, user: {text}, startedMs}` |
| `segment.start` | `{turnId, ...the segment as it starts}` (a note's text, a tool's name and call) |
| `segment.delta` | `{segId, text}` or `{segId, argsText}`: text to append |
| `segment.end` | `{segId, tokens, ms}` |
| `tool.state` | `{callId, state, ms?, plus?, minus?, source?, result?}` |
| `approval.request` | `{approvalId, callId, tool, args, summary, why, subject, diff?, plus?, minus?}`: `why` names the mode and rule that asked; `subject` is the rule "Allow for this chat" would add (`fs.write(src/core/**)`, `shell(nuke *)`) |
| `approval.resolved` | `{approvalId, decision, by?}`; `withdrawn` when the turn ended first |
| `question.request` / `question.resolved` | `{questionId, callId, question, options, allowText}` / `{questionId, answer, by}` |
| `tasks.changed` | `{items: [{id, title, status, note?}]}`; `status` is `pending`, `in_progress`, `done`, `skipped` |
| `queue.changed` | `{queued: [text]}` |
| `files.changed` | `{files: [{path, plus, minus}]}`: what the AI wrote in this chat |
| `context.changed` | `{window, used, parts: [{name, tokens}]}`: the last request's prompt by part against the model's window (0 when unknown) |
| `turn.stats` | `{turnId, tokensIn, tokensOut, ttftMs, genMs}` after each request |
| `turn.end` | `{turnId, stop, recoveries, tokensIn, tokensOut, ttftMs, genMs, endedMs}`: `stop` is `finished`, `user_cancelled`, `no_model`, `interrupted` or the stop reason that paused the chat (`docs/12` taxonomy); `recoveries` the recovery actions taken, in order ("retried after http_error 503 (1x)") |

## Git

The `ied.git` plugin's server code (`plugins/git/code/`, a built-in on the plugin API, `docs/26` "Built-ins on the API") runs the **git CLI** (`docs/99` #14) in the repository without the server lock, with no optional locks (`GIT_OPTIONAL_LOCKS=0`: a status never rewrites the index and wakes the watcher), `-c core.quotepath=off`, `GIT_TERMINAL_PROMPT=0`, `LC_ALL=C`, `GIT_EDITOR=:` and machine formats (`status --porcelain=v2 -z`, `log` and `for-each-ref` with unit-separator fields). A command that changes something goes to the workspace's `git` Output source with what git said. A workspace without git on `PATH` (or the `git.path` setting) has no repositories, and `git.repos` says why. Git never runs in an untrusted workspace: a repository's configuration (`core.fsmonitor`, hooks) runs programs, so `gitrepos:<wsId>` says `untrusted` and its repositories come back when it is trusted.

**Repositories.** A workspace's repositories are the one its folder is in (`rev-parse --show-toplevel`, which may be above it) and those found in its folders down to `git.scanDepth` (2) levels, `build`-like folders skipped (the `fs.search` default excludes); a submodule is one too. A thread of the git service finds them when the workspace opens (or is trusted) and lists them on `gitrepos:<wsId>`. Each gets a `repoId` for this server run (`r1`, `r2`, ...). A change under a repository's folder or its git dir (the workspace watcher, "Files changed on disk"; a git dir outside the workspace folder gets a watcher of its own) reads its status again after 150 ms of quiet, at most once a second while changes keep coming; a change in the git dir other than `objects/`, `logs/` and lock files reads its refs too. A method that changes something reads both before it answers.

| Method | Params | Answer |
|---|---|---|
| `git.repos` | `wsId` | `{repos: [{repoId, root, name, kind}], error, discovering}`: `root` workspace-relative (`""` the folder itself; `..` when the repository is above it), `kind` `repo` or `submodule`; `error` is `untrusted`, or why git cannot run; `discovering` while the repositories are being found. The same as `gitrepos:<wsId>`'s snapshot |
| `git.log` | `repoId, skip?, limit?` (200, at most 2000), `query?` | `{commits: [commit], more}`, newest first in date order over every ref (`--all --date-order`), the stashes left out. `commit` is `{oid, parents, author, email, authorMs, subject}`. `query` keeps the commits whose message, author or email has it (ASCII letters in any case) or whose hash starts with it; `skip` and `limit` then count matches |
| `git.show` | `repoId, oid` | `{oid, parents, author, email, authorMs, committer, committerEmail, commitMs, subject, body, files: [{path, oldPath?, status, added, removed, binary}]}`: `status` `A`, `M`, `D`, `R`, `C`, `T`; the files against the first parent (a root commit: against nothing) |
| `git.diff` | `repoId, path`, and `oid` (a commit's change to the file) or `staged` (true: index against HEAD; false: the work tree against the index), `context?` (3) | `{path, oldPath?, binary, hunks: [{oldStart, oldLines, newStart, newLines, header, lines}], truncated}`: `lines` start with `+`, `-` or a space; a diff over 2 MB is cut (`truncated`) |
| `git.fileAt` | `repoId, path, rev` (a commit, `:` the index, `""` the work tree) | `{text, binary, missing, truncated}` (4 MB at most): a side of a side-by-side diff |
| `git.stage` / `git.unstage` | `repoId, paths` | `{}`: `add --` / `restore --staged --` (a new repository: `rm --cached`) |
| `git.stagePatch` | `repoId, path, staged, hunk, header, lines?` | `{}`: hunk `hunk` of the file's `git.diff` (with `staged` false, the work tree's diff; true, the staged one), or the line indexes `lines` of it, staged (or unstaged); the patch is built here (`plugins/git/code/patch`: the lines not chosen are left out or kept as context) and applied with `apply --cached --recount` (`-R` to unstage). `header` is the hunk's header as the caller saw it: `conflict` when the diff has changed since |
| `git.discardPatch` | `repoId, path, hunk, header, lines?` | `{}`: the same for the work tree's diff, taken out of the work tree (`apply -R --recount`) |
| `git.discard` | `repoId, paths` | `{}`: tracked files back to the index (`restore --`); untracked ones to the IED trash (`docs/10` "Files") |
| `git.commit` | `repoId, message, amend?, signoff?` | `{oid}`; `bad_request` when nothing is staged (and not `amend`) or the message is empty |
| `git.lastMessage` | `repoId` | `{message}`: HEAD's, for amend |
| `git.checkout` | `repoId, branch, remote?` | `{}`: `switch` to a local branch; with `remote`, `branch` is a remote one (`origin/x`) and a local branch tracking it is made. Local changes go along when they can; when the checkout would overwrite them: `conflict` with `data.files`, and the client offers to stash them first |
| `git.branch` | `repoId, name, from?` (HEAD), `checkout?` | `{}` |
| `git.branchDelete` | `repoId, name, force?` | `{}`; an unmerged branch without `force`: `conflict` with git's words |
| `git.branchRename` | `repoId, name, to` | `{}` |
| `git.stash` | `repoId, message?, untracked?, keepIndex?` | `{}` |
| `git.stashApply` | `repoId, index, pop?` | `{}`; `conflict` with git's words when it does not apply cleanly |
| `git.stashDrop` | `repoId, index` | `{}` |
| `git.fetch` | `repoId, remote?` (all), `prune?` | `{opId}` |
| `git.pull` | `repoId, mode?` (`git.pullMode`: `merge`, `rebase`, `ffOnly`) | `{opId}` |
| `git.push` | `repoId, remote?, branch?` (the current), `setUpstream?`, `forceWithLease?` | `{opId}`; never a plain force |

Fetch, pull and push are **operations**: one at a time per repository (`busy` otherwise), run with `--progress`, their lines streamed as `op.progress` and their end as `op.end`. They never ask for credentials on a terminal; Git Credential Manager may show its own window on the server's desktop (F-GIT-25 puts its prompts in the UI later).

| Topic | Events | Snapshot |
|---|---|---|
| `gitrepos:<wsId>` | `repos.changed` (the snapshot again) | `{repos, error, discovering}` (`git.repos`) |
| `git:<repoId>` | `status.changed {head, changes}`, `refs.changed {refs}`, `op.progress {opId, kind, line, percent?}`, `op.end {opId, kind, ok, error?}` | `{repoId, root, head, changes, refs, op}` |

- `head`: `{branch, oid, detached, upstream, ahead, behind, state}`; `branch` empty when detached or before the first commit, `state` `""`, `merging`, `rebasing`, `cherryPicking`, `reverting` or `bisecting`.
- `changes`: `{staged, unstaged, untracked, conflicted}`, each a list of `{path, oldPath?, status}` (`status` as in `git.show`; `?` untracked; a conflict's `status` is its two letters, `UU`, `AA`, ...). At most 5000 a list (`truncated: true`).
- `refs`: `{branches: [{name, oid, upstream, ahead, behind, current, gone}], remotes: [{name, url, branches: [{name, oid}]}], tags: [{name, oid}], stashes: [{index, message, oid}], worktrees: [{path, branch, oid, current, locked}], submodules: [{path, oid, state}]}`; the oids are full, `gone` an upstream that no longer exists.
- `op`: the running operation `{opId, kind, title, lines}` (its last 20 lines), or null.

The commit graph's lanes are laid out by the client from the commits' parents (`plugins/git`), so paging only appends.

## Liveness

- Client pings every 20 s; the server closes sockets silent for 60 s. The server also pings (WebSocket ping frames) to keep proxies alive.
- The client reconnects with jittered exponential backoff (0.5 s -> 15 s cap) and shows the reconnecting banner (F-SHL-34). Input typed while disconnected stays in the UI and is sent (with its `opId`) after resume.

## Versioning

- `proto` is an integer. The server supports the current and previous version during upgrades; a mismatch beyond that sends `bye { reason: "protocol-mismatch" }` and the client reloads to fetch the matching frontend from the server (the server always serves its own matching client, so this only happens across a server update).
- Additive changes (new topics, new event kinds, new optional fields) do not bump `proto`. Clients ignore unknown event kinds.

## Local control channel (CLI <-> server)

The `ied` CLI and the second-instance handoff (F-SRV-2) use a named pipe `\\.\pipe\ied-<user-sid>` with newline-delimited JSON-RPC 2.0. The pipe's only access entry is the current user and remote clients are refused (Bedrock `Net::LocalListener`). A client refuses a pipe whose server runs as another user, which defeats a squatted name. The CLI never talks HTTP, so it works even when the HTTP bind is LAN-only.

The pipe name is the single-instance lock: the server claims it before anything else, and a second server exits with code 3 ("another ied-server runs for this user").

| Method | Result |
|---|---|
| `url` | `{url}`: `http://<host>:<port>/?t=<launch token>`, which signs a browser in once (`docs/11`) |
| `open {path, line?, col?}` | `{wsId, path, url}`. `path` is absolute. A folder becomes a workspace unless one holds it already. A file opens in the workspace that holds it, else its folder becomes one; `path` in the result is workspace-relative, and the server topic gets `workspace.opened`. The URL ends in `#open=<wsId>/<path>[:<line>[:<col>]]`, which the web client reads once it has signed in: it shows that workspace and, once its layout is in place, runs the `fileOpener` command (`docs/02`), the editor's showing the file at the place and a Markdown file's preview beside it. |
| `status` | `{serverVersion, pid, url (without a token), workspaces, clients}` |
| `stop` | `{}`, then every socket gets `bye {reason: "shutdown"}`, the pipe closes and the process ends |
| (later) `open {wait: true}` | blocks until the document closes (git's editor) |

Errors are JSON-RPC 2.0's: -32700 not JSON, -32600 not a request, -32601 no such method, -32602 bad params, and -32000 for anything a method refuses (a path that does not exist).

**The CLI** (`cli/`, `ied.exe`) follows `ied [<path>[:<line>[:<col>]]]`, `ied status`, `ied url` and `ied stop`. When no server runs, it starts `ied-server.exe` from beside itself (the installed layout) or the sibling build folder. That server has no shared streams, so it gets a hidden console of its own and outlives the terminal. It is started with `--static` set to the nearest `frontend/` folder, and falls back to a free port when 8765 is taken. The CLI then opens the browser at the returned URL. `ied stop` returns once the pipe has closed.

## Open items

- Binary frames for terminal/profiler bulk (v1.1) - measure first.
- Whether `doc` rebase needs to support two simultaneous human writers (current answer: no, lease model).
