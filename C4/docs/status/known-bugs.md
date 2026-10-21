# Known bugs

Every open bug in the C4 compiler, runtime, Bedrock and tools. Add an entry when a bug is found;
delete it when the fix lands.

Next ID: **BUG-321**

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
| BUG-320 | A bound C header that declares memmove (string.h, windows.h) breaks the prelude's memmove | Interop | High |
| BUG-317 | Include folders (-I, a project's `includes`) never reach CBind or the foreign-source compile | Interop | High |
| BUG-318 | With --projects and a relative --int-dir, CBind produces no bindings | Interop | Medium |
| BUG-319 | CBind binds a destructor for abstract classes without one, and the shim warns | Interop | Low |

---

## Parser, lexer and preprocessor

## Model, templates and semantics

## HIR and codegen

## Interop, runtime and Bedrock

### BUG-320 A bound C header that declares memmove breaks the prelude's memmove
- **Area:** Interop
- **Severity:** High
- **Status:** Open
- **Found:** 2026-10-21
- **Symptom:** with `extern "c" include "s.h"` where s.h includes `<string.h>` (as `<windows.h>` does), Bedrock's calls fail: "Text/string.cf (48:3) error: no function named memmove". The prelude declares `[link("memmove")] void memmove(void*, void*, uint64)` and the binding adds the CRT's `void* memmove(void*, const void*, size_t)`: two declarations of one C function, so the call is ambiguous, and the error calls it missing. memcpy, memset and any other prelude function a C header declares break the same way.
- **Where:** `Bedrock/Prelude/prelude.ch`; the bindings `Interop/InteropPass.cpp` appends; the call diagnostics for an ambiguous free function

### BUG-317 Include folders never reach CBind or the foreign-source compile
- **Area:** Interop
- **Severity:** High
- **Status:** Open
- **Found:** 2026-10-21
- **Symptom:** a bound header (`extern "c" include "x.h"`) that `#include`s a header from a `-I` folder or a project's `includes` fails: "CBind failed on p/x.h: fatal error: 'y.h' file not found", and an `extern "c" source` that includes it fails to compile the same way. Only the extern block's own `include_dir()` reaches clang.
- **Where:** `Interop/InteropPass.cpp` `RunFolder`: `BindingFlags{ language, defines, includeDirs, targetFlags }` and `ForeignObject.includeDirs` take the block's include dirs only; the command line's / project's folders are used only to find the named header. The binding cache key must include them.

### BUG-318 With --projects and a relative --int-dir, CBind produces no bindings
- **Area:** Interop
- **Severity:** Medium
- **Status:** Open
- **Found:** 2026-10-21
- **Symptom:** `C4.exe --projects projects.toml -o out/r.exe --int-dir out2` reports that CBind "produced no bindings" for a header beside the `.ch`; an absolute `--int-dir` works.
- **Where:** the interop pre-pass's paths under the intermediate folder (relative to a different working folder than the one CBind runs in)

### BUG-319 CBind binds a destructor for abstract classes without one, and the shim warns
- **Area:** Interop
- **Severity:** Low
- **Status:** Open
- **Found:** 2026-10-21
- **Symptom:** binding WebView2.h in C++ mode makes the generated shim emit about 296 `-Wdelete-abstract-non-virtual-dtor` warnings: every COM interface (an abstract class with no user-declared destructor) gets a `Finalize` wrapper calling `~T()`.
- **Where:** CBind's destructor wrappers; an abstract class with no accessible virtual destructor should get no destructor binding

## Tools

## Diagnostics and tests
