# Known bugs

Every open bug in the C4 compiler, runtime, Bedrock and tools. Add an entry when a bug is found;
delete it when the fix lands.

Next ID: **BUG-455**

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
| BUG-421 | C4LSP puts a dependency's declarations in the root project of a multi-project program | Tools | Low |
| BUG-435 | Intermittent internal compiler error (0xC0000005) or corrupt partition bitcode compiling a large split program | Codegen | High |
| BUG-441 | `threading.thread.sleep_is_precise` fails in the gates while the machine is busy: sleeps land on the 15.6 ms tick | Runtime | Medium |

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

### BUG-435 Intermittent internal compiler error (0xC0000005) or corrupt partition bitcode compiling a large split program
- **Area:** Codegen
- **Severity:** High
- **Status:** Needs re-test (most likely not a compiler bug: the machine's memory corrupts, see Notes)
- **Found:** 2026-10-31
- **Symptom:** Compiling Claymore's `ClaymoreEditor` (1080 files, `-O3 -g --incremental --script-host`, the default
  threads) fails about 4 times in 8 with no change between runs: `C4: internal compiler error: exception 0xc0000005`
  at a different address each time, or `error: cannot read a partition of the module back: Invalid ID`. The next run
  of the same command usually succeeds. Symbolized against the toolchain's `C4.pdb`, the crashes were in
  `llvm::formLCSSAForInstructions` under `RunPipeline` / `AssemblePartition` on a `ParallelFor` worker
  (`Compiler/ObjectEmission.cpp`), in `Scripting::CallEmitter::Emit`, and in `llvm::ValueEnumerator::ValueEnumerator`
  (bitcode writing): three phases, which with the unreadable bitcode points at a module corrupted earlier, during the
  parallel emission of the partitions (D-81), rather than a fault in any one of them.
- **Repro:** in `C:/GameDev/Claymore/Claymore/ClaymoreEditor`: `C:/GameDev/IED/toolchains/c4/dev/C4.exe --projects
  C:/GameDev/Claymore/build/int/Development-windows-x64/ClaymoreEditor/projects.toml -O3 -g --incremental -l user32
  -l gdi32 -l dwmapi -l imm32 -l shell32 -l shcore -l advapi32 -l d3d12 -l dxgi -l kernel32 --script-host --int-dir
  <a scratch folder> -o <scratch>/ClaymoreEditor.exe`, run several times (touch a source between runs or use a fresh
  int dir, so the partitions are assembled again).
- **Where:** suspects: `ProgramModel`'s `mutable` caches filled lazily by const getters (`m_LifecycleMethods`,
  `m_TypeScopes`, `m_MethodRecords`, `m_RecordBySignature`, `m_ReturnLists`, `m_TypeInstantiations`, ...) reached by
  the partitions' emitters at once in `CodeGenDriver::EmitPlaced` / `EmitImports` (`m_Lowering.Pool().Run`), where
  only `QueryFunction` and `CensusCounterType` take `m_ModelLock` (`Backend/CodeGen/ModuleContext.cpp`); or any
  cache that hands one partition's thread an `llvm::Type`/`Constant` of another partition's context (each
  `PartitionEmitter` has an `LLVMContext` of its own, and a value of another context in a module writes bitcode the
  reader rejects with "Invalid ID").
- **Notes:** 2026-10-31 investigation: the build machine (Ryzen 7 7800X3D, 2x32 GB DDR5) bugchecked 6 times in 12 days
  (System log: `0x1A` MEMORY_MANAGEMENT x4, `0x12B` FAULTY_HARDWARE_CORRUPTED_PAGE, `0xBE`), once at 09:43 under a
  parallel clang build. In 38 compiles of snapshots of the repro (HEAD and the working tree, fresh and incremental,
  up to 3 at once) C4 never crashed; the one failure was in the front end, `type not found: LPCWSTr` for the
  `LPCWSTR` of WindowingWin32's translated `Native.ch`: one bit (0x20) flipped. clang itself crashed with access
  violations on 3 files of a 14-way parallel compile (and the C++ driver retries clang crashes for the same reason,
  `plugins/cpp/nuke/driver/objects.py`), and C4LSP's 6 crashes since 10/30 are at 6 different places of AST
  teardown. Random faults in unrelated programs under memory load point at the hardware, not at C4. Re-test once
  the memory passes a memory test (or on another machine): the repro above, 10 runs; if it still fails, build C4
  with AddressSanitizer (`-fsanitize=address` on C4, C4Lib and Core) and run it there. Found on the way and fixed:
  BUG-436, a parked parser thread could take its include response on a spurious wakeup before the scheduler
  dispatched it (`CompilerMessageQueue` now hands a response over only when it is released).

## Interop, runtime and Bedrock

## Tools

### BUG-421 C4LSP puts a dependency's declarations in the root project of a multi-project program
- **Area:** Tools
- **Severity:** Low
- **Status:** Open
- **Found:** 2026-10-28
- **Symptom:** In the Claymore workspace the editor shows, on `Packages/Renderer/RHI/src/Rhi/Frame/Renderer.ch` and
  `Commands/CommandList.ch`, `unresolved type for field 'm_Log' in 'RendererDesc': 'Log' is declared in project
  'ClaymoreClient', and project 'Rhi' does not depend on 'ClaymoreClient'` (and `type not found: Logger` in
  `Frame/ShaderWatcher.ch`), while C4.exe compiles the same files with the same build description
  (`build/int/Development-windows-x64/ClaymoreClient/projects.toml`, where `Log` is a project of its own that `Rhi`
  depends on) without an error. `Windowing::` names in the same files resolve. Another symptom, seen 2026-10-28 in the
  same workspace: every file of `Packages/Renderer/Render` and the files using `Render::` in `ClaymoreEngine` show
  `redefinition of namespace 'Render': it is already a function (RhiD3D12/Native.ch:16133)` (a function of the SDK
  headers RhiD3D12's `Native.ch` includes `extern "c++"`; Render does not depend on RhiD3D12) and then `not a
  namespace path: Render`, while `nuke test` builds the same programs clean.
- **Repro:** open Claymore (the package `Packages/Log` was added after the session's language server started) and
  read the problems of `Packages/Renderer/RHI/src/Rhi/Frame/Renderer.ch`; not yet reduced to a small workspace.
- **Where:** C4LSPLib `CompilationSession` (which program serves a file shared by several, `RegisterProjects`),
  `ProgramModel::OutOfProjectReach`.
- **Notes:** reduce it first: a two-program workspace whose shared module depends on a module added later. A third
  symptom (2026-10-29, same workspace): after `Procedural = { source = ... }` was added to `ClaymoreEngine`'s
  `[dependencies]`, `Scene/Pipeline/TerrainRenderer.ch` showed `include not found: 'Procedural/Terrain/Heightfield.ch'`
  for hours of builds that compiled it clean, though `.ied/local/lsp-options/ClaymoreEngine.json` (the programs the
  server is told through `initializationOptionsFiles`) was rewritten during them; it cleared when IED restarted. So
  either the pool's `SendOptionsFilesIfChanged` did not fire for that rewrite, or C4LSP took the new programs without
  re-resolving the includes of files it had analyzed: check `LspProgramsTest`'s configuration-change case with a
  dependency added to a program that already holds the file.
