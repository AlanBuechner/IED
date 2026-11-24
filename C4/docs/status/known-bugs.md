# Known bugs

Every open bug in the C4 compiler, runtime, Bedrock and tools. Add an entry when a bug is found;
delete it when the fix lands.

Next ID: **BUG-504**

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
| BUG-503 | Every pinned job system pins its submitting thread to core 0: two such programs starve each other | Bedrock | Medium |

---

## Parser, lexer and preprocessor

## Model, templates and semantics

## Interop, runtime and Bedrock

### BUG-503 Every pinned job system pins its submitting thread to core 0: two such programs starve each other
- **Area:** Bedrock
- **Severity:** Medium
- **Status:** Open
- **Found:** 2026-11-24
- **Symptom:** With `JobSystemDesc.pinToCores` the thread that makes the job system is pinned to core 0
  (`Thread::PinCurrentToCore(0)`), whatever else runs on the machine. Two processes that pin this way share logical
  processor 0: when one keeps it busy, the other's main thread waits for it while every other processor is idle.
  Seen 2026-11-24: an LSEditor left running (its main loop at 100% on processor 0, mask 0x1) made Claymore's
  `nuke test` 20-100 times slower (a test of 1.1 s took 127 s; the test process used 67 s of CPU in 78 minutes),
  every test passing; closing LSEditor brought it back at once. Claymore pins in every `GameScene` that has no host
  and was not told `LeaveCores(true)` (ClaymoreEngine `Scene/GameScene.cf:68`).
- **Repro:** start two programs that make a job system with `pinToCores = true` and keep the main thread busy in
  one; time anything on the other's main thread. `GetThreadGroupAffinity` shows both main threads at mask 0x1.
- **Where:** `C4/Bedrock/Threading/jobs.cf:168-180` (`JobSystem` start: `PinCurrentToCore(0)`, workers on cores
  1..n), `C4/C4Runtime/ThreadRuntimeWin32.c` / `ThreadRuntimePosix.c` `c4_thread_pin_to_core`.
- **Notes:** a hard pin to a fixed core is only safe when the program owns the machine (a shipped game, a
  benchmark). Directions: pin the submitting thread softly (an ideal processor, `SetThreadIdealProcessor`, not an
  affinity mask), or leave it unpinned and pin only the workers, or pin only when asked by the program's command
  line; tests and editors should not pin at all. Separately, LSEditor's main loop never waited (worth a look in the
  LS project: an editor with nothing to draw should block on its messages).

