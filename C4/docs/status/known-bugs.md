# Known bugs

Every open bug in the C4 compiler, runtime, Bedrock and tools. Add an entry when a bug is found;
delete it when the fix lands.

Next ID: **BUG-394**

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
| [BUG-386](#bug-386-pointer-free-container-buffers-are-scanned-by-the-collector) | Pointer-free container buffers are scanned by the collector | Bedrock | Medium |

---

## Parser, lexer and preprocessor

## Model, templates and semantics

## Interop, runtime and Bedrock

### BUG-386 Pointer-free container buffers are scanned by the collector
- **Area:** Bedrock
- **Severity:** Medium
- **Status:** Open
- **Found:** 2026-10-23
- **Symptom:** every `Core::Array`, `HashMap`, `List` and `Queue` registers its element buffer as a scan region
  whatever the element type, so the floats, indices and vertices an engine keeps in them are scanned word by word at
  every collection. A region is native memory the write watch does not see, so a concurrent cycle copies it whole
  at each stop and scans it whole at the last: one 256 MiB `Core::Array<float>` beside 64 MiB of live objects took
  the longest stop of a concurrent cycle from 0.5-2.9 ms to 30-36 ms (D-79 promises about 1 ms) and `GC::Collect`
  from 6-9 ms to 14-16 ms. Numbers that look like heap addresses also keep garbage alive.
- **Repro:** `GC::SetConcurrent(0)`; 600,000 live `new` objects of 64 bytes; `Core::Array<float> a; a.Resize(64 *
  1024 * 1024);`; allocate until `GC::GetStats().collections` moves; the longest of `GC::LastPauses`.
  `c4_gc_region_count()` is 1. The same 256 MiB as 16,384 slots of 16 KiB in a keep-all `GC::Pool`, each slot
  holding a pointer, gives 1.4-1.6 ms.
- **Where:** `Bedrock/Container/storage.ch` (`ResizeScannedBytes`, used by `array.cf`), `hashtable.cf`, `list.cf`,
  `queue.cf`, `Threading/jobqueue.cf`. No compile-time query gives C4 code `TypeHandle::MayHoldPointers`, the
  compiler's own noscan decision.
- **Notes:** fixed by [plan B70](plan.md#b70---pointer-free-buffers-are-not-scanned-bug-386): a `holds_pointers(T)`
  intrinsic, and containers that register a region only when it is true.
