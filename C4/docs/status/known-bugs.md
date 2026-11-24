# Known bugs

Every open bug in the C4 compiler, runtime, Bedrock and tools. Add an entry when a bug is found;
delete it when the fix lands.

Next ID: **BUG-508**

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
| BUG-504 | An object at a heap mapping's first byte is kept alive by any word equal to a 64 MiB boundary | Runtime | Medium |
| BUG-505 | With `-fstack-maps` a build differs from run to run | Codegen | Medium |
| BUG-506 | With `-fstack-maps` a pointer argument passed on the stack may not be read by the callee's record | Runtime | High |
| BUG-507 | With `-fstack-maps` at `-O0` a function many values live across fails to compile ("ran out of registers") | Codegen | High |

---

## Parser, lexer and preprocessor

## Model, templates and semantics

### BUG-505 With `-fstack-maps` a build differs from run to run
- **Area:** Codegen
- **Severity:** Medium
- **Status:** Open
- **Found:** 2026-11-24
- **Symptom:** the Bedrock benchmark built twice with `-fstack-maps -O2` gives objects that differ in about 30 bytes;
  without the flag they are the same. Served builds, object caches and byte-identity checks see a change that is none.
- **Where:** `Compiler/StackMapRecords.cpp` `Records`: the value slots are made while it walks an `unordered_map` keyed
  by the calls' addresses, so their allocas (and the frame's layout) come in another order each run.

### BUG-507 With `-fstack-maps` at `-O0` a function many values live across fails to compile ("ran out of registers")
- **Area:** Codegen
- **Severity:** High
- **Status:** Open
- **Found:** 2026-11-24
- **Symptom:** ScriptTest `Prepare`'s host built `-fstack-maps -O0`: "ran out of registers during register allocation" in
  `PrepareOnWorker`.
- **Where:** `Compiler/StackMaps.cpp` / `StackMapRecords.cpp`: each value live across a call is a deopt operand in a
  register (`use-registers-for-deopt-values`); `-O0`'s RegAllocFast cannot spill a statepoint's register operands.

## Interop, runtime and Bedrock

### BUG-504 An object at a heap mapping's first byte is kept alive by any word equal to a 64 MiB boundary
- **Area:** Runtime
- **Severity:** Medium
- **Status:** Open
- **Found:** 2026-11-24
- **Symptom:** IsolatedTest `MemoryGC/StackMaps` on Linux `-O2` fails in about one run of fifty ("round 1: finalized 40
  of 41"): the program's first object, at the heap's first mapping's first byte, stays alive after it is dead. libc's
  start frame (`__libc_start_main`, read whole) holds a value equal to the 64 MiB boundary its own mapping starts at,
  which ASLR sometimes makes the heap's first mapping's start too.
- **Where:** `C4Runtime/GC/GCPageHeap.c` `add_arenas`: a mapping's first span starts at the mapping's base.

### BUG-506 With `-fstack-maps` a pointer argument passed on the stack may not be read by the callee's record
- **Area:** Runtime
- **Severity:** High
- **Status:** Open (found reading the code)
- **Found:** 2026-11-24
- **Symptom:** LLVM names a deopt operand that is an incoming stack argument (Windows from the fifth, Linux from the
  seventh) by its slot, as a Direct location; `GC/GCStackMaps.c` `scan_value` skips Direct locations as a slot's
  address. Where the caller holds the object no more, it can be collected while the callee still uses it.
- **Where:** `C4Runtime/GC/GCStackMaps.c` `scan_value`.
