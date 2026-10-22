# conformance - protocol v1 black-box suite

Drives any server over its `/ws` endpoint and checks the behavior `docs/10` promises: ordering, resume exactly-once, snapshot after expiry, gap -> resync, coalescing, idempotent `opId`, cancellation, document lease and rebase. It runs against `tools/mock-server/` today and must run unchanged against the C4 `ied-server` (M1 exit, `docs/18`). Stdlib only (`unittest` + `tools/pylib/ws.py`).

```bash
python -I tools/conformance/run_conformance.py                                # spawns the mock on a free port
python -I tools/conformance/run_conformance.py --url ws://127.0.0.1:8765/ws   # any running server
python -I tools/conformance/run_conformance.py -k resume -v                   # a subset, verbose
python -I tools/conformance/run_conformance.py --server <ied-server.exe>       # starts it, signs in with its launch token
```

A server that authenticates (`docs/11`) wants its session cookie on every socket: `--server` spends the token from the
server's `OPEN` line and sends the cookie it gets; with `--url`, pass `--cookie name=value`.

Every frame the server sends is validated against `shared/host-api/protocol/frames.v1.schema.json`; any invalid frame fails the test that received it.

## What it covers

| Module | Checks |
|---|---|
| `test_session.py` | welcome fields; `proto` mismatch -> `bye protocol-mismatch`; first frame must be hello; ping/pong `ts`; invalid `req` -> `bad_request`; unknown method -> `not_found`; idle sockets closed, talking ones kept; presence events |
| `test_requests.py` | responses matched by id and allowed to overtake; all 11 error codes pass through with `data`; duplicate in-flight id rejected; `cancel` of an in-flight request, of a finished/unknown one (ignored), of a stream a request opened; `opId`: original result returned, survives reconnect, per client, joins a request still in flight, replays errors, rejects reuse for another method |
| `test_topics.py` | `not_found` topics; fresh sub = snapshot then contiguous events; seq 0 snapshot; replay from a cursor; cursor at head = nothing replayed; `unsub`; identical sequences for every subscriber; resync by re-subscribing; independent topics |
| `test_resume.py` | killed socket resumes with every missed event exactly once (also mid-stream); ring expired -> snapshot; foreign epoch -> snapshot; vanished topics omitted; server restart -> `bye server-restart`, new epoch, snapshots, durable document text kept |
| `test_backpressure.py` | a slow client gets a `gap` starting right after its last event, nothing more for that topic until resync, responses never dropped; resync by snapshot and by replay; replay larger than the budget -> snapshot; output floods coalesced into fewer frames with no lost or reordered bytes |
| `test_search.py` | `fs.search`: one match per line with its UTF-16 column and preview, any case or with case, `node_modules` skipped by default and searched with `exclude: []`, a regex anchor, `include` globs, `maxResults` and `truncated`, open documents searched with their unsaved edits, an empty query or a bad regex is `bad_request` |
| `test_documents.py` | first opener holds the lease, others get `forbidden {holder}`; edits broadcast to every subscriber including the sender with `origin`; `doc.takeLease` moves the lease and notifies; a stale edit is rebased over a server edit; an overlapping one is `conflict {version}` and succeeds after resync; an edit retried with its `opId` after reconnect applies once; columns are UTF-16 units; `fs.write` is revision-checked |

## What a server must implement to run it

Besides the frames, the suite uses these methods (shapes in `tools/mock-server/README.md`): `doc.open`, `doc.edit`, `doc.takeLease`, `doc.save`, `fs.read`, `fs.write`, and the **test hooks** below. A real server exposes `testhooks.*` only when started with a test flag (e.g. `ied-server --test-hooks`), never in normal use. Hook topics live under the plugin-style prefix `testhooks/` so they cannot collide with product topics.

| Method | Params | Result / effect |
|---|---|---|
| `testhooks.info` | - | `{version: 1, methods}` |
| `testhooks.configure` | `outboundBudget?`, `coalesceBytes?`, `idleTimeoutMs?` | applies to the calling socket only; returns the effective values |
| `testhooks.topic` | `ringEvents?`, `ringBytes?`, `coalescible?` | creates `testhooks/stream:<n>`; snapshot `{events, lastIndex, textLength}` |
| `testhooks.emit` | `topic, count, kind? = "tick", size?` | publishes `count` events at once: `tick {i, text}` or (kind `output`) `output {text: "<i>|<size x's>\n"}`; returns `{first, last, seq}` |
| `testhooks.stream` | `topic, count, intervalMs?, size?` | background `tick` events; ends with `stream.end {reason: done\|cancelled, emitted}`; `cancel` with this request's id stops it |
| `testhooks.delay` | `ms, result?` | sleeps (cancellable); returns `{sleptMs, result, execution}` where `execution` counts delay executions server-wide |
| `testhooks.counter` | `name?` | increments and returns `{name, value}` (the idempotency target) |
| `testhooks.fail` | `code, message?, data?` | fails with exactly that error |
| `testhooks.restart` | - | responds, then simulates a restart: new epoch, rings and opId cache lost, durable state kept, every socket gets `bye server-restart` |
| `testhooks.scratch` | `lines?` | creates a fresh text file `"line 0\nline 1\n..."`; returns `{wsId, path, text, revision}` |
| `testhooks.serverEdit` | `docId, changes, origin?, baseVersion?` | applies an edit as the server (like an AI tool or a reload) |
| `testhooks.gc` | - | the real server only (not the mock): a full garbage collection, then `{collections, liveBytes, heapBytes}`; the idle soak's leak measure |

The decisions the suite pins down beyond `docs/10` (sub result shape, replay-vs-budget rule, opId details, UTF-16 columns, ...) are listed in `tools/mock-server/README.md` under "Protocol decisions".

## Files

`run_conformance.py` (runner, spawns the mock), `harness.py` (base test case: one control client, frame validation, reconnect helper), `iedclient.py` (strict client: cursors, duplicate counting, pause/abort), `tests/`.
