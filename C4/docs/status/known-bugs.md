# Known bugs

Every open bug in the C4 compiler, runtime, Bedrock and tools. Add an entry when a bug is found;
delete it when the fix lands.

Next ID: **BUG-357**

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
| BUG-356 | A source, macro output, project file or script cache path longer than MAX_PATH is not read or written | Driver | Low |

---

## Parser, lexer and preprocessor

## Model, templates and semantics

## HIR and codegen

### BUG-356 A source, macro output, project file or script cache path longer than MAX_PATH is not read or written
- **Area:** Driver
- **Severity:** Low
- **Status:** Open
- **Found:** 2026-10-22
- **Symptom:** a C4 source whose own path passes 260 characters (a deep project folder) is not found or read; the same
  for the macro pass's output, project files and the script host's caches and sources.
- **Where:** `SourceManager/Source.cpp`, `SourceManager/SourceManager.cpp`, `Macro/MacroPass.cpp`,
  `Compiler/ProjectFile.cpp`, `Compiler/RuntimeObjects.cpp`, `Script/HostInterface.cpp`, `Script/ScriptDll.cpp`,
  `Script/ScriptSources.cpp` read and write with the standard streams and `std::filesystem`,
  which do not take long paths (unless Windows' `LongPathsEnabled` is on and the program's manifest opts in).
- **Notes:** the build-output side (objects, interop, CBind, the output, intermediate and interop folders, the
  `.pdb`, the script cache) goes through `Compiler/FileIo` since BUG-354; these go the same way.

## Interop, runtime and Bedrock

## Tools

## Diagnostics and tests
