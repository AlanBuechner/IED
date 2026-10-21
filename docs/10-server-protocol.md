# 10 - Server protocol v1 (client <-> server)

## Purpose

Resolve `docs/99` #2: the wire contract between the web client and `ied-server`. It must carry AI token streams, build/terminal output floods, LSP diagnostics and editor edits without dropping anything, and make "close the browser, come back tomorrow" seamless. Everything here is generic: no topic, method or payload names anything language- or engine-specific.

Status: **design v1, ready for implementation.** Machine-readable frame schema: `shared/host-api/protocol/frames.v1.schema.json`.

## Transport

| Channel | Used for |
|---|---|
| `GET /` and static paths | the web app (HTML/JS/CSS, plugin UI modules under `/plugins/<id>/ui/...`, Monaco, xterm) |
| `GET /ui-kit/<path>`, `GET /codicons/<name>.svg`, `GET /vendor/<library>/<path>` | the components shared by the shell and plugin UI modules (`shared/ui-kit/`), one codicon each (`third_party/codicons`), and the install's web libraries (`vendor/web/`: Monaco, xterm.js), cacheable for a day |
| `POST /api/auth/*` | login, logout, token exchange (`docs/11`) |
| `GET /api/blob/<id>` | large binary payloads (images, trace chunks, file downloads), referenced from frames by id |
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
| `ws:<wsId>` | `tree.changed`, `trust.changed`, `solution.changed` | workspace summary (`kind` folder, file or solution), trust, the solution model (`docs/03`) |
| `settings:app`, `settings:<wsId>` | `settings.changed` | tier files (revision, problems) and every setting's effective value with its source (`docs/07`) |
| `plugins` | `plugins.changed` | every plugin's summary and state (`docs/02`) |
| `layout:<wsId>:<deviceClass>` | `layout.changed {revision, layout, by}` | `{revision, layout}`: the layout document the client last stored for the workspace and device class (`docs/15`), verbatim; revision 0 with `layout: null` until one is. `layout.set {wsId, deviceClass, layout, expectedRevision}` stores one (an object up to 256 KB, or null to forget it) and answers `{revision}`; an old `expectedRevision` gets `conflict {revision}`. Kept in `.ied/local/layout/<deviceClass>.json` of a solution, and for a folder or a file in `<app data>/workspaces/<key>/layout/`, `key` the first 16 hex digits of the SHA-256 of the workspace folder's canonical path |
| `doc:<docId>` | `doc.edit`, `doc.saved`, `doc.lease`, `doc.diskChanged`, `doc.renamed` | text + version + lease holder + dirty |
| `diag:<wsId>` | `diag.set {path, source, items}` (LSP diagnostics, UTF-16 ranges; `docs/24`) | all current diagnostics, `{files: {<path>: {<source>: [diagnostic]}}}` |
| `chat:<chatId>` | `turn.start`, `segment.start`, `segment.delta`, `segment.end`, `tool.state`, `approval.request`, `tasks.changed`, `scratch.changed`, `turn.end`, `context.changed` | chat header + last N turns (older via `chat.history` request) |
| `run:<runId>` | `step.start`, `output` (source, text), `step.end`, `run.end` | task run state + output tail |
| `out:<wsId>:<sourceId>` | `append {text}` (coalescible), `clear` | `{sourceId, label, tail}`: the last 64 KB, cut at a line start. Every workspace has the `ied` source, the server's own log with each line stamped `[hh:mm:ss]`; `out.sources {wsId}` lists a workspace's sources and `out.clear {wsId, sourceId}` empties one |
| `term:<termId>` | `data`, `resize`, `exit`, `lease` | screen state (serialized via xterm headless) + scrollback tail |
| `dbg:<sessionId>` | `stopped`, `continued`, `threads`, `breakpoints`, `output`, `terminated` | session state (threads, current frame) |
| `prof:<captureId>` | `stats`, `progress` | capture summary (timeline data is fetched via requests/blobs) |
| `git:<repoId>` | `status.changed`, `refs.changed`, `op.progress` | status + refs |

Plugins add topics under their own prefix (`<pluginId>/<kind>:<id>`); the hub is generic.

**Files changed on disk.** The server watches each workspace folder, so changes made by anything else (a git checkout, another editor, a build) arrive like its own:

- `tree.changed {changes: [{path, change: created|changed|deleted}], overflow?: true}`. Changes are gathered until the folder has been quiet for 100 ms, at most a second, and folded per path: created then deleted is nothing, a rename is a delete and a create. `overflow` means the system dropped changes, so the client lists the folder again. The repository folder (`.git/`) is not reported.
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
- When a client's queue for a **coalescible** topic (`out:*`, `run:*`, `term:*`) passes a threshold, the server merges pending `output`/`data` events into one larger event (fewer frames, same bytes).
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
| `fs.list` | `wsId, path` | `{path, entries: [{name, path, kind: file\|dir, size?, revision?}]}` |
| `fs.read` | `wsId, path` | `{path, text, revision}` |
| `fs.write` | `wsId, path, text, expectedRevision?` (0: must not exist) | `{path, revision}`; folders are made |
| `fs.mkdir` | `wsId, path` | `{path}` |
| `fs.rename` | `wsId, from, to` (a file or a folder) | `{path, revision?}`; open documents follow (`doc.renamed`) |
| `fs.delete` | `wsId, path, expectedRevision?, recursive?` (a folder needs `recursive`) | `{trashId}`: the file or folder is moved to the trash |
| `fs.search` | `wsId, query, regex?, caseSensitive?, include?, exclude?, maxResults?` | `{matches: [{path, line, col, preview}], truncated}`: the first match of each line, `col` in UTF-16 units, `preview` the line trimmed to 200 characters; `include` / `exclude` are globs on the path |
| `trash.list` | `wsId?` (only that workspace's) | `{entries: [{id, root, path, kind, deletedMs, bytes}], bytes}`, newest first |
| `trash.restore` | `id` | `{wsId?, path}`: moved back where it was (`conflict` when something is there now) |
| `trash.purge` | `id` or `all: true` | `{}`: deleted for good |

**Trash** (F-OPS-13): nothing a person or an AI deletes through the server is lost at once. A deleted file or folder moves to `<data>/trash/<id>/` (the server's data folder, `%LOCALAPPDATA%/IED/` by default) with an `entry.json` saying where it came from; `trash.restore` puts it back, `trash.purge` removes it. When the trash holds more than 4 GB the oldest entries are purged. Watchers see a trash move as a delete and a restore as a create, like any change.

## Document editing protocol

Server-owned buffers (F-EDT-1) with a simple, sufficient scheme:

- Each `doc` has an integer `version`. The **lease holder** sends `doc.edit { docId, baseVersion, changes: [ {range, text} ... ], opId }`.
- If `baseVersion == version`, the server applies, increments, and broadcasts `doc.edit` to all subscribers (including the sender, who uses it as the ack).
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
{ "t":"evt","topic":"chat:42","seq":1001,"kind":"turn.start","data":{"turnId":"u7","mode":"agent","group":"default"} }
{ "t":"evt","topic":"chat:42","seq":1002,"kind":"segment.start","data":{"turnId":"u7","segId":"s1","type":"thinking","model":"qwen3.8-27b@spark"} }
{ "t":"evt","topic":"chat:42","seq":1003,"kind":"segment.delta","data":{"segId":"s1","text":"Let me look at"} }
{ "t":"evt","topic":"chat:42","seq":1040,"kind":"segment.end","data":{"segId":"s1","tokens":212,"ms":4100} }
{ "t":"evt","topic":"chat:42","seq":1041,"kind":"segment.start","data":{"segId":"s2","type":"tool","tool":"lsp_definition","callId":"k1"} }
{ "t":"evt","topic":"chat:42","seq":1042,"kind":"segment.delta","data":{"segId":"s2","argsText":"{\"symbol\":\"Ri"} }
{ "t":"evt","topic":"chat:42","seq":1050,"kind":"tool.state","data":{"callId":"k1","state":"running"} }
{ "t":"evt","topic":"chat:42","seq":1051,"kind":"tool.state","data":{"callId":"k1","state":"ok","ms":38,"result":{"preview":"..."}} }
{ "t":"evt","topic":"chat:42","seq":1099,"kind":"turn.end","data":{"turnId":"u7","stop":"finished","tokensIn":18233,"tokensOut":911} }
```

- Delta frames are batched server-side every ~30 ms (or 512 bytes) to keep frame counts sane on phones.
- `stop` reasons are an enum shared with the UI (`finished`, `user_cancelled`, `max_tokens_continued`, `stalled_retried`, `nudged`, `budget_paused`, `error`, `interrupted`, `awaiting_approval`, `awaiting_answer`) - see `docs/12`.

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
| `open {path, line?, col?}` | `{wsId, path, url}`. `path` is absolute. A folder becomes a workspace unless one holds it already. A file opens in the workspace that holds it, else its folder becomes one; `path` in the result is workspace-relative, and the server topic gets `workspace.opened`. The URL ends in `#open=<wsId>/<path>[:<line>[:<col>]]`, which the web client reads once it has signed in. |
| `status` | `{serverVersion, pid, url (without a token), workspaces, clients}` |
| `stop` | `{}`, then every socket gets `bye {reason: "shutdown"}`, the pipe closes and the process ends |
| (later) `open {wait: true}` | blocks until the document closes (git's editor) |

Errors are JSON-RPC 2.0's: -32700 not JSON, -32600 not a request, -32601 no such method, -32602 bad params, and -32000 for anything a method refuses (a path that does not exist).

**The CLI** (`cli/`, `ied.exe`) follows `ied [<path>[:<line>[:<col>]]]`, `ied status`, `ied url` and `ied stop`. When no server runs, it starts `ied-server.exe` from beside itself (the installed layout) or the sibling build folder. That server has no shared streams, so it gets a hidden console of its own and outlives the terminal. It is started with `--static` set to the nearest `frontend/` folder, and falls back to a free port when 8765 is taken. The CLI then opens the browser at the returned URL. `ied stop` returns once the pipe has closed.

## Open items

- Binary frames for terminal/profiler bulk (v1.1) - measure first.
- Whether `doc` rebase needs to support two simultaneous human writers (current answer: no, lease model).
