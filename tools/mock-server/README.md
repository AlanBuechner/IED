# mock-server - protocol v1 with fixture data

A Python stand-in for `ied-server` (`docs/10`): one WebSocket per tab, topics with seq numbers and ring buffers, snapshots, resume, gaps, coalescing, opId idempotency, cancellation, server-owned documents with lease + rebase - over an in-memory fake workspace, a scripted chat, task runs with ANSI output and an echo terminal. It lets the frontend be built, screenshot-tested and resume-tested before the C4 server exists. `tools/conformance/` keeps it honest. Stdlib only.

```bash
python -I tools/mock-server/mock_server.py                       # http://127.0.0.1:8765, debug console at /_mock/
python -I tools/mock-server/mock_server.py --static frontend      # serve the frontend at /
python -I tools/mock-server/mock_server.py --port 0 --time-scale 0 --quiet   # tests: free port, no fixture delays
python -I tools/mock-server/mock_server.py --host 0.0.0.0         # phones on the LAN (the mock has no auth)
python -I -m unittest discover -s tools/mock-server/tests -t tools/mock-server/tests
```

The first stdout line is `LISTENING http://<host>:<port>`. Every frame the mock sends is validated against `shared/host-api/protocol/frames.v1.schema.json` (a mock bug fails loudly); `--frames-log x.frames.jsonl` records both directions, and `tools/schema-check` can validate that file.

| Flag | Default | |
|---|---|---|
| `--time-scale` | 1.0 | multiplies chat delta and build line delays (0 = as fast as possible) |
| `--idle-timeout` | 60 | seconds of silence (no frame of any kind, including pongs to the server's 20 s pings) before a socket is closed |
| `--outbound-budget-mb` / `--coalesce-kb` | 4 / 256 | per-client queue budget before a `gap`; queued bytes before output events merge |
| `--tree-files N` | 0 | N extra files under `generated/` (virtualized tree testing) |
| `--allow-origin`, `--allow-host` | - | extra Origin / Host values (e.g. a frontend dev server on another port) |
| `--no-validate` | - | skip per-frame schema validation (floods are faster) |

## HTTP

| Path | |
|---|---|
| `GET /ws` | the protocol socket. Host must be a loopback name or the bind address (DNS-rebinding guard); `Origin`, when present, must be this server or an `--allow-origin` (docs/11). Bound to `0.0.0.0` the Host check is off. |
| `GET /healthz` | `{ok, serverVersion, sessionEpoch}` |
| `PUT /api/upload` -> `GET /api/blob/<id>` | attachment blobs (in memory) |
| `GET /_mock/` | debug console: hello/resume with cursors in sessionStorage, subscribe, send requests, drop and resume the socket |
| `GET /plugins/<id>/<path>`, `GET /w/<wsId>/plugins/<id>/<path>` | a loaded built-in plugin's files (the repository's `plugins/`), as the real server serves them |
| `GET /w/<wsId>/files/<path>` | a workspace image (`fixtures/workspace_files.py` `IMAGES`: `shots/screen.png`, not in the tree since the mock's files are text); image types only, with `nosniff` and a sandboxing CSP |
| `GET /ui-kit/<path>`, `GET /codicons/<name>.svg` | `shared/ui-kit/`, and one codicon from `third_party/codicons` |
| `GET /*` | files from `--static`, else a redirect to `/_mock/` |

Not implemented (later milestones): auth (`docs/11` login, tokens, cookies), `/w/<ws>/port/<n>`, binary frames.

## Topics

| Topic | Snapshot | Events |
|---|---|---|
| `server` | `serverVersion`, `workspaces`, `clients` (presence), `notifications` | `device.connected`, `device.disconnected`, `notification`, `notification.dismissed` |
| `ws:w1` | workspace summary, `trusted`, `plugins`, `chats` | `tree.changed {changes:[{path, change: created\|changed\|deleted}]}`, `trust.changed`, `chat.created` |
| `layout:w1:<desktop\|tablet\|phone>` | `{revision, layout}`: the client's layout document (`docs/15`), verbatim; revision 0 holds null | `layout.changed {revision, layout, by}` |
| `plugins` | `{plugins: [summary]}` of the repository's built-in plugins (`plugins/*/ied-plugin.json`, schema-checked) | `plugins.changed` |
| `doc:<docId>` | `docId, wsId, path, text, version, lease: {holder}, dirty, languageId` | `doc.edit {version, changes, origin, rebasedFrom?, opId?}`, `doc.lease {holder}`, `doc.saved {version, revision}`, `doc.diskChanged {revision}` |
| `diag:w1` | `{files: {path: {source: [diagnostic]}}}` | `diag.set {path, source, items}` |
| `chat:<chatId>` | header, `state`, `tasks`, last 20 `turns` (in-flight segment text included), `hasMore`, `pendingApprovals` | `turn.start {user}`, `segment.start/delta/end`, `tool.state`, `approval.request`, `approval.resolved`, `tasks.changed`, `turn.end {stop, tokensIn, tokensOut, error?}` |
| `run:<runId>` | `status, steps, exitCode, tail` (64 KB) | `step.start`, `output {step, source, text}`, `step.end`, `run.end {status, exitCode, ms}` |
| `out:w1:<build\|test\|run\|ied>` | `{sourceId, label, tail}` | `append {text}`, `clear` |
| `term:<termId>` | `cols, rows, alive, exitCode, lease, scrollback` | `data {data}`, `resize`, `lease`, `exit {code, reason}` |
| `testhooks/stream:<n>` | `{events, lastIndex, textLength}` | `tick {i, text}`, `output {text}`, `stream.end {reason, emitted}` |

Ring limits follow `docs/10`: chat 20k events / 8 MB, run 50k / 16 MB, term 5 MB, others 2k events / 4 MB. `out:`, `run:` and `term:` are coalescible.

## Methods

Params are objects; ids such as `w1`, `c1`, `d1`, `r1`, `t1` come from the fixtures and earlier results.

| Method | Params -> result |
|---|---|
| `server.info` | -> `serverVersion, sessionEpoch, proto, mock, testHooks` |
| `server.notify` / `server.dismiss` | `text, level?` -> `{id}` / `id` |
| `ws.list`, `ws.setTrust` | - / `wsId, trusted` |
| `fs.list` | `wsId, path?` -> `entries [{name, path, kind, size?, revision?}]` (directories first) |
| `fs.read` | `wsId, path` -> `text, revision` |
| `fs.write` | `wsId, path, text, expectedRevision?` (0 = must not exist) -> `revision`; `conflict {revision}` when stale. Reloads an open clean document (origin `disk`) or flags a dirty one (`doc.diskChanged`) |
| `fs.delete`, `fs.mkdir`, `fs.rename` | `wsId, path, expectedRevision?` / `wsId, path` / `wsId, from, to` |
| `fs.search` | `wsId, query, regex?, caseSensitive?` -> `matches [{path, line, col, preview}], truncated` |
| `doc.open` | `wsId, path` -> `docId, version, holder` (the first opener gets the lease) |
| `doc.edit` | `docId, baseVersion, changes [{range: {start: {line, col}, end}, text}]` + frame `opId` -> `version, changes, rebased` |
| `doc.takeLease`, `doc.save` | `docId` -> `holder` / `version, revision` |
| `layout.set` | `wsId, deviceClass, layout (object or null), expectedRevision` -> `revision`; `conflict {revision}` when stale; at most 256 KB; in memory only |
| `plugins.list`, `plugins.contributions` | `wsId?` -> the topic's summaries / `{plugins: [{id, source, contributes, ui?}]}` of the loaded ones |
| `plugins.setEnabled`, `plugins.reload`, `plugins.approve` | `id, enabled` / - / `id` (always `not_found`: the mock has no app plugins) |
| `plugins.call` | `wsId?, name, args?` -> always `not_found`: the mock runs no plugin code (docs/26 section 2) |
| `out.sources`, `out.clear` | `wsId` / `wsId, sourceId` |
| `task.list`, `task.runs` | `wsId` |
| `task.run` | `wsId, task, params?` -> `runId, topic`. Tasks: `build` (succeeds, 1 warning), `build-broken` (fails with an error), `test` (2 pass, 1 fail), `run` (build, then the program), `flood` (`params.lines`, default 50000) |
| `task.cancel` | `runId` |
| `term.create` | `wsId, cols?, rows?` -> `termId, topic`. Commands: `help`, `clear`, `colors`, `echo <text>`, `flood <n>`, `exit` |
| `term.input`, `term.resize`, `term.takeLease`, `term.close`, `term.list` | lease holder only for `term.input` |
| `chat.list`, `chat.create` | - / `wsId?, title?, mode?` |
| `chat.send` | `chatId, text, mockScript?` + `opId` -> `turnId, script`; `busy` while a turn runs |
| `chat.stop`, `chat.history`, `chat.approve` | `chatId` / `chatId, before?, limit?` / `chatId, approvalId, decision: allow\|deny` (first answer wins; later ones get `conflict {decision, by}`) |
| `testhooks.*` | see `tools/conformance/README.md` |

Chat scripts (`fixtures/chat_scripts.py`) are picked by `mockScript` or by a keyword in the text: `screen` -> `snap` (a plugin tool whose result shows `shots/screen.png`), `big prompt` -> `prefill` (40 s of llama.cpp prompt progress, `turn.progress`, then an answer), `slow server` -> `estimate` (a 60 s estimated read, then an answer), `approv`/`run `/`test` -> `approval`, `long`/`essay` -> `long`, `error`/`fail` -> `error`, else `default` (thinking, two tool calls, tasks, markdown answer). Seeded chats: `c1` (two finished turns), `c2` (empty).

## Protocol decisions the mock had to make

`docs/10` does not pin these down; the mock and the conformance suite implement them, and they should be folded into `docs/10` once the owner agrees:

1. The first frame must be `hello` (else close 1002); a second `hello` closes 1002; binary frames close 1003 (reserved for v1.1). An invalid `req` with an integer id gets `bad_request`; any other invalid frame closes 1002.
2. `sub` answers `res {result: {topics: {<key>: "ok"|"snapshot"|"not_found"}}}` before any snapshot or replayed event. Re-subscribing to a subscribed topic resyncs it (queued events for it are dropped first).
3. `welcome.resume` omits topics that no longer exist; the client drops their state.
4. Replay happens only when the ring still holds `cursor+1` **and** the replay fits the client's remaining outbound budget; otherwise a snapshot. Without this, a resync after a gap re-overflows and gaps again forever.
5. After a `gap`, the server sends nothing more for that topic until the client re-subscribes.
6. A coalesced event carries the seq of the last event merged into it, so seqs are increasing but not always contiguous.
7. `opId` is the frame-level `req.opId`, per `clientId`, kept 10 minutes; a duplicate while the original is in flight waits for it; errors are replayed too; reusing an opId for another method is `bad_request`; opIds do not survive a restart.
8. Liveness counts any WebSocket frame, including pongs to the server's pings, because browsers throttle timers in background tabs and JSON pings can arrive late.
9. `cancel` with the id of a request that opened a stream (`task.run`, `chat.send`, `testhooks.stream`) stops that stream.
10. Document positions are 0-based `{line, col}` with `col` in UTF-16 code units (what JavaScript strings index); changes in one edit refer to the pre-edit text and must not overlap; overlapping a concurrent change is `conflict {version, reason: "overlap"}`; editing without the lease is `forbidden {reason: "lease", holder}`.
11. Seq numbers continue across a restart; only the epoch changes.
12. Additive event kinds beyond the `docs/10` table: `device.disconnected`, `notification.dismissed`, `approval.resolved`, `chat.created`, `stream.end`.

## Layout

| File | Role |
|---|---|
| `mock_server.py` | entry point, flags, `start()` for in-process tests |
| `front.py` | HTTP: Host/Origin checks, upgrade, healthz, blobs, static, console |
| `session.py` | one socket: hello/welcome/resume, sub/unsub, req/cancel, liveness |
| `hub.py`, `outbox.py`, `rpc.py` | topics + rings; per-client queue with coalescing and gaps; dispatch, errors, opId cache |
| `state.py` | the server object, config, simulated restart |
| `services/` | `presence`, `workspace` (fs), `docs` + `textedit` (rebase), `layout`, `outputs` (output + diagnostics), `tasks`, `terminals`, `chat`, `testhooks` |
| `fixtures/` | workspace files, chat scripts, task output |
| `console/index.html` | the debug console |
| `tests/` | fixture behavior over real sockets (chat, approvals, runs, terminal, layout, fs, HTTP checks) |
