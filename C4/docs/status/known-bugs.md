# Known bugs

Every open bug in the C4 compiler, runtime, Bedrock and tools. Add an entry when a bug is found;
delete it when the fix lands.

Next ID: **BUG-503**

## Rules for this file

- **One entry per bug.** If one root cause has several symptoms, list them in one entry.
- **IDs are permanent.** Take the "Next ID" above and bump it. Never renumber or reuse an ID, so
  commits, tests and code comments can refer to `BUG-012` safely.
- **Remove the entry when it is fixed**, in the same change as the fix. The fix must come with a test
  that would have caught it (see [../tools/05-test-suites.md](../tools/05-test-suites.md)). Also remove
  the matching "Current implementation" note on the feature's page. The commit message keeps the history.
- **Pre-existing is not a reason to keep a bug.** Per `AGENTS.md`, every known bug gets fixed.
- **Status** is `Open` (confirmed by a repro or by reading the code) or `Needs re-test` (reported
  earlier, may already be fixed; re-test, then fix or delete).
- **Severity:**
  - **High** - miscompiles, memory unsafety, compiler crash, or valid programs that cannot be written.
  - **Medium** - wrong behaviour with a workaround, valid code rejected, a language rule not enforced.
  - **Low** - diagnostics quality, performance, tooling polish.
- Design questions that are not bugs go to [roadmap.md](roadmap.md). Unbuilt features go to the
  roadmap backlog, not here.

### Entry template

```text
### BUG-NNN Short title
- **Area:** Parser | Model | HIR | Codegen | Preprocessor | Runtime | Bedrock | Tools | Diagnostics | Tests
- **Severity:** High | Medium | Low
- **Status:** Open | Needs re-test
- **Found:** YYYY-MM-DD
- **Symptom:** what goes wrong, from the user's point of view
- **Repro:** smallest program or command (optional)
- **Where:** files / functions involved
- **Notes:** fix direction, related bugs, constraints (optional)
```

---

## Index

| ID | Title | Area | Severity |
|---|---|---|---|
| BUG-441 | `threading.thread.sleep_is_precise` fails in the gates while the machine is busy: sleeps land on the 15.6 ms tick | Runtime | Medium |
| BUG-497 | The IED master on Linux aborted in `malloc` (heap corruption) relaying WebSocket frames after half an hour of a chat | Runtime | High |

---

## Parser, lexer and preprocessor

## Model, templates and semantics

### BUG-441 `threading.thread.sleep_is_precise` fails in the gates while the machine is busy: sleeps land on the 15.6 ms tick
- **Area:** Runtime
- **Severity:** Medium
- **Status:** Open
- **Found:** 2026-11-05
- **Symptom:** In `nuke gates` (the `bedrock` step, and again under `gc-concurrent`) the fastest of three runs of ten 2 ms
  `Threading::Thread::Sleep`s took 238 ms (714 ms for the case), the system tick's 15.6 ms each, and the case failed
  (`threading_tests.cf:172`). Run alone right after (`BedrockTests.exe --filter "threading.thread.*"`) it took 82 ms
  and passed. Several other chats were building and testing on the machine at the time.
- **Where:** `Threading::Thread::Sleep` (Bedrock's precise sleep), `Bedrock/Tests/threading_tests.cf`.
- **Notes:** a frame limiter sleeping on the tick would cap a game well under its target, so this matters beyond the
  test: find whether the precise timer is lost under load (a waitable timer refused, a resolution request dropped),
  or whether the case asks more than a loaded machine can give.
  2026-11-21 (chat J): not reproduced - the case (now the fastest of up to ten runs) passed in two full `nuke gates`
  runs and 8 of 8 times alone under a full server-checks run (26-29 ms for ten 2 ms sleeps: the high-resolution timer
  held). Still open, not reproduced: `c4_thread_sleep_ns` fell back to `Sleep` without a word when the high-resolution timer
  could not be made, which would explain the tick-length sleeps. 2026-11-22 (chat L): the fallback now says so once on
  standard error (`c4: CreateWaitableTimerExW failed (error N): Thread::Sleep ends on the system tick`); the case passed in
  two more full `nuke gates` runs. Close this when a loaded run fails without that line (the cause is then the case's
  demand on a busy machine, not the timer), or fix what the line names.

## Interop, runtime and Bedrock

### BUG-497 The IED master on Linux aborted in `malloc` (heap corruption) relaying WebSocket frames after half an hour of a chat
- **Area:** Runtime
- **Severity:** High
- **Status:** Open
- **Found:** 2026-11-21
- **Symptom:** the owner's IED on Linux (prebuilt release of 2026-11-22, Debian-built `dev` toolchain): the master server
  died with `C4: fatal: the program aborted (SIGABRT)` about 35 minutes after a workspace was opened and a chat ran in
  it. glibc's `malloc` aborted, which it does when it finds its heap already corrupt: the stack is where the damage was
  found, not where it was done. Stack (thread of the browser's connection): `abort` <- `malloc` <- `CountedResize` <-
  `Core::Array<uint8>::Append` (`Bedrock/Container/storage.ch:30`) <- `Http::WriteFrame` (`Bedrock/Http/frames.cf:81`)
  <- `Http::WebSocket::SendHeld` (`websocket.cf:143`) <- `Ied::Proxy::Pump` (`src/web/Proxy.cf:164`, the browser's
  message sent on to the workspace server) <- `Http::AcceptWebSocket`'s closure <- `Http::AnswerNext`.
- **Repro:** none yet: once, in normal use (one desktop window, a workspace server with a Claude chat streaming
  through the master's tunnel, the config server mirror reconnecting every few minutes, Tor and SearXNG stopped).
- **Where:** unknown. The frame being built is a fresh local array, and every WebSocket send holds `sending`; so
  something else in the master freed or overran a block earlier.
- **Ruled out (chat F, 2026-11-22):** the R16 precise collector. The crashed build's toolchain (the release's
  `ied-release-linux-build` volume, built 17:18 local) had every R16 change but no stack maps (`-fstack-maps` is off by
  default) and Linux collects stop-the-world only (no write tracking), so only heap maps, typed regions and mapped roots
  were precise. (1) The maps, audited against their types: unions, bases, template elements, every container's
  `gcType` (hash entries and control bytes are separate buffers), reflected values, pools, thread_locals; no pointer kept
  in a number field or in raw byte storage anywhere in the server or Bedrock (one pointer-as-integer: an identity key,
  never dereferenced); the map cache's callers all hold the lock. (2) The runtime's new map check (`C4_GC_VERIFY_MAPS`,
  runtime/01) found 0 missed words: the server's 557 unit tests on Windows and on Linux (with `C4_GC_STRESS=25`,
  `C4_GC_POISON=1` and glibc's checks), the server checks on Windows (553 tests, 63 server processes), and 15 minutes
  of WebSocket relay through the master on Linux (79 GiB, 11612 connections, stress 50, poison, glibc's checks) and 10
  on Windows (35 GiB). (3) The release's own Linux server under glibc's checks (`LD_PRELOAD=libc_malloc_debug.so.0`,
  `glibc.malloc.check=3`, `MALLOC_PERTURB_=165`) and poison: 30 minutes of relay (`.ied/local/f16/relay/relay_load.py`:
  6 workers writing and reading 1 KiB-1 MiB files through `/w/<wsId>/ws`, reconnecting; 150 GiB, 22495 connections)
  and the server checks (553, failures all of the bare container) - no abort, no heap error. Also ruled out: BUG-499
  (the server binds no C function taking an aggregate by value - every `[link]` takes numbers and pointers, no C header
  is included), and a stopped thread's registers on Linux (the suspend handler runs on the thread's own stack, where the
  kernel saves them, inside the range scanned).
- **Notes:** what the loads did not have, so where to look next: a Claude chat streaming (the CLI as a child process:
  `Process/` on Linux, its pipes and the `ReapWhenEnded` thread), TLS through the system's OpenSSL (`Net/tcp.cf`: a
  session is freed by `Close`, which no other thread may be in a call on), an hour of real use. Run the owner's IED with
  the checks on - `IED_HEAP_CHECKS=1 ied-desktop` (or `ied <folder>`; `ied stop` first): the launcher starts the master
  with `C4_GC_VERIFY_MAPS=~/.local/state/ied/logs/heap-checks.log C4_GC_POISON=1 LD_PRELOAD=libc_malloc_debug.so.0
  GLIBC_TUNABLES=glibc.malloc.check=3 MALLOC_PERTURB_=165` and its standard error appended to that file, which its
  workspace servers inherit (docs/21 "Heap checks") - so the damage stops nearer its cause, or under ASan / Valgrind.
  Checked in the Linux builder (master and workspace server have the variables, a deliberate map miss and an aborted
  server's fatal report are in the file); still open until a run with the checks on ends in a report or an abort at the
  cause (or an hour of real use ends in neither). The crash also left the workspace's server running unreachable, which
  kept the folder from opening again: that part was IED's and is fixed (`--master-pid`, `src/Main.cf` `WatchMaster`).
