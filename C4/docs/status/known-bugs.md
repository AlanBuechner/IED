# Known bugs

Every open bug in the C4 compiler, runtime, Bedrock and tools. Add an entry when a bug is found;
delete it when the fix lands.

Next ID: **BUG-510**

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
| BUG-508 | A build through the build server links a reused body's call by a name the build no longer emits | Tools | High |
| BUG-509 | The compiler crashed now and then splitting a whole-program module for code generation | Codegen | High |

---

## Parser, lexer and preprocessor

## Model, templates and semantics

## Interop, runtime and Bedrock

### BUG-509 The compiler crashed now and then splitting a whole-program module for code generation
- **Area:** Codegen
- **Severity:** High
- **Status:** Open
- **Found:** 2026-11-24
- **Symptom:** `nuke gates`' step `script-debug-O2` (ScriptTest `Templates`' host, `--script-host -O2 -g`) failed once
  with `internal compiler error: exception 0xc0000005`. Symbolized (the compiler of HEAD c5af6cb8 with X's -O0
  stack-map change, whose code runs only after this point): `llvm::Value::~Value` <- `llvm::Function::~Function` <-
  `Module` function list `erase` <- `llvm::Module::~Module` <- the `Expected<unique_ptr<Module>>` of
  `BuildShare` (`Compiler/ModulePartitions.cpp:397`, the end of a split thread's share) <- `SplitModuleToBitcode`'s
  worker thread. 42 rebuilds of the same host right after (30 with stack maps, 6 with, 6 without) did not crash.
- **Repro:** none yet: `C4.exe <ScriptTest Templates host> -o host.exe --script-host -O2 -g` in a loop, maybe under load.
- **Where:** `C4Lib/src/Compiler/ModulePartitions.cpp` `BuildShare`: each thread reads its own lazy copy of the module's
  bitcode into an `LLVMContext` of its own, makes partitions from it, and destroys the copy - a value still used (by
  metadata, a value handle or another module's value) when its function is deleted would crash there.
- **Notes:** recorded by X; not reproduced, so not fixed. A race in the split (shared state between the threads' copies)
  or a partition module outliving what it refers to are the first suspects. Likely seen again 2026-11-25 (not symbolized, so
  not certainly the same): `nuke test` in LS (the dev compiler built 05:38 UTC from b8c33ea708, modified; NUKE_JOBS=4 at low
  priority) crashed compiling the ClaymoreScriptHost test harness with `exception 0xc0000005 at C4.exe+0x16b0efd`; its sources
  unchanged, `nuke test ClaymoreScriptHost` right after compiled it and its 16 tests passed.

## Tools

### BUG-508 A build through the build server links a reused body's call by a name the build no longer emits
- **Area:** Tools
- **Severity:** High
- **Status:** Open
- **Found:** 2026-11-24
- **Symptom:** in Claymore, `nuke test` (with the dev toolchain built at 2026-11-24 23:07 UTC from 26e5f261 with R1 stage 2b's serve changes, committed as f6b4f6af) failed to link the AnimeRenderingEditor test harness: `lld-link: error: undefined symbol: ?sse?s_AssetManager_ForgetMissing_0`, referenced by `EngineCore.cf:256` in `Claymore_EngineCore_BeginFrame_0` - where `AssetManager::Update` (Packages/Assets `AssetManagerFrame.cf`, unchanged since 2026-11-19) calls the private `ForgetMissing`. Every other harness of the run (ClaymoreEngine, Editor and the rest) linked the same call; a second `nuke test AnimeRenderingEditor` failed the same way; after deleting that harness's `build/int/.../AnimeRenderingEditor/tests` it compiled and linked, and its 20 tests passed. None of the sources involved had changed: the build server's kept state (the program's earlier build, made by an earlier compiler build of the same day) gave the reused or inlined body a call by a decorated name the new build did not emit.
- **Repro:** not reduced: it went with the deleted folder. Likely: a program built through its server, the compiler rebuilt from a checkout whose name-decoration or inlining changed, the program built again without a change to its sources.
- **Where:** `C4Lib/src/Serve/` (BodyEdits, BodySwap: what a server reuses from the build before), Nuke `nuke/c4/server.py` (a server of a compiler since rebuilt answers `stale` - by the compiler file's time and size - so the state reused was its own, or the objects left in the folder were)
- **Notes:** found while verifying an unrelated Claymore change (the editor's sidebar). Not yet known whether the stale name came from the server's kept state or from an object left in the folder by the earlier compiler; where the `?sse?s_` decoration is made is not yet found. Seen again 2026-11-25 with the compiler built 03:53 UTC (19ca798c): the LightingEditor test harness failed to link `__c4_reflect_copy_Core_3a_3aArray_3cClaymore_3a_3aSceneSystemSlot_2a_3e.local`, referenced from a reused `reflection-*.obj` - a reflection object kept from an earlier build naming a local copy function the new build did not emit; its sources unchanged. Deleting `build/int/.../LightingEditor/tests` made it link.
