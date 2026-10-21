# Known bugs

Every open bug in the C4 compiler, runtime, Bedrock and tools. Add an entry when a bug is found;
delete it when the fix lands.

Next ID: **BUG-324**

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
| BUG-323 | A non-pure nomem asm block is merged or hoisted at -O2 | Codegen | High |
| BUG-322 | A failed relink leaves the old program beside a new .c4i and .pdb | Tools | Medium |

---

## Parser, lexer and preprocessor

## Model, templates and semantics

## HIR and codegen

### BUG-323 A non-pure nomem asm block is merged or hoisted at -O2
- **Area:** Codegen
- **Severity:** High
- **Status:** Open
- **Found:** 2026-10-21
- **Symptom:** an `asm(nomem)` block without `pure` (docs/language/16 section 1's `rdtsc` example) runs once where
  the program reaches it twice: two reads of the timestamp counter at -O2 return the same value.
- **Repro:** `ReadTsc()` with `asm(nomem, nostack) { "rdtsc"; out("eax") low; out("edx") high; }`, called before and
  after a sleep: the delta is 0 at -O2.
- **Where:** `Backend/CodeGen/AsmEmitter.cpp` marks every `nomem` call `memory(none)`, which lets LLVM treat it as a
  value whatever its `sideeffect` flag says.
- **Notes:** as Rust lowers it: `memory(none)` / `memory(read)` and `willreturn` only with `pure`; a non-pure
  `nomem` block touches inaccessible memory only.

## Interop, runtime and Bedrock

## Tools

### BUG-322 A failed relink leaves the old program beside a new .c4i and .pdb
- **Area:** Tools
- **Severity:** Medium
- **Status:** Open
- **Found:** 2026-10-21
- **Symptom:** a `--script-host` build writes `<out>.c4i` before linking, and lld writes `<out>.pdb` before it
  commits the executable. When the link then fails (the old program is still running, so its file is locked), the
  folder holds the old executable with the new interface and debug info: the host refuses every script ("the
  interface is another build's") and a debugger rejects the PDB.
- **Repro:** run an IED server built with `--script-host`, change a source, rebuild it.
- **Where:** `src/ProgramOutput.cpp` (PrepareScriptHost writes the interface), `Compiler/BuildUtils.cpp` (LinkWithLLD)
- **Notes:** write the interface only after the link succeeds; link the PDB under a temporary name (with
  `/PDBALTPATH` naming the final one) and rename it into place once the executable is.

## Diagnostics and tests
