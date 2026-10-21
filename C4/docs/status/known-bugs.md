# Known bugs

Every open bug in the C4 compiler, runtime, Bedrock and tools. Add an entry when a bug is found;
delete it when the fix lands.

Next ID: **BUG-295**

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
| BUG-294 | A redefined variable, enum, union or alias does not say where the name already is | Diagnostics | Low |

---

## Parser, lexer and preprocessor

## Model, templates and semantics

## HIR and codegen

## Interop, runtime and Bedrock

## Tools

## Diagnostics and tests

### BUG-294 A redefined variable, enum, union or alias does not say where the name already is
- **Area:** Diagnostics
- **Severity:** Low
- **Status:** Open
- **Found:** 2026-10-20
- **Symptom:** two files of a module that both define `const int32 kSlice = 1;` in one namespace fail with
  `redefinition of variable 'kSlice'` and nothing more; the other definition has to be searched for. Types and
  functions say `it is already a type (other.cf:3)` since BUG-291.
- **Where:** `ModelBuilderValues.cpp` (variables), `ModelBuilderTypes.cpp` (enums, aliases),
  `ModelBuilderUnions.cpp` (unions): they build the message themselves instead of `ModelBuilder::Redefinition`.

