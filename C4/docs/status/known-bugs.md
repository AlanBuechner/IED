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
- **Status:** Open - for the owner: the evidence (2026-11-25, below) points at the machine, not the code
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
  unchanged, `nuke test ClaymoreScriptHost` right after compiled it and its 16 tests passed. Perhaps related, seen by X
  2026-11-25 (the compiler of c5af6cb8 with X's change; `nuke test` in Claymore under a full machine): compiling WindEditor's
  test harness crashed in `llvm::ValueEnumerator::enumerateMetadataImpl` <- `ValueEnumerator` <- `WriteBitcodeToFile` <-
  `ModuleBitcode` <- `PartitionBitcode` <- `WriteCachedObjects`' `ParallelFor` worker (`ObjectEmission.cpp:446`) - again a
  thread's own copy of a split module, again no stack-map code in the path; the next run compiled it (all 84 harnesses
  passed). The change since X's clean runs is f6b4f6af.
  The machine is a suspect too: Windows logged a fatal hardware error (WHEA-Logger, 2026-11-22) and four unexpected
  shutdowns (Kernel-Power 41: 11-22 twice, 11-24 at 22:09 and 23:26), and since the last one every Edge and WebView2 start
  dies with 0xc000001d at the same offset of msedge_elf.dll, whose bytes there are an ordinary `js`.
- **Investigated 2026-11-25 (chat AA; scripts and logs in `.ied/local/aa/`):**
  - *Not reproduced, on three paths.* A copy of the dev compiler (842ffb8d): the Templates host `--script-host -O2 -g`
    whole-program (BuildShare) 30 times alone and 20 times with three more compiles beside it; incremental
    (PartitionBitcode on the output's threads) 200 alone and 100 with three beside; Claymore's WindEditor harness as Nuke
    compiles it 15 alone and 10 with two beside - 0 failures in 755 compiles, and every run kept the same objects (65, 7,
    260: identical code each time). The compiler built for Linux in Docker (the tree at 14:50): the same host 30 + 20x4
    whole-program and 30 + 20x4 incremental, 0 failures. C4Testing's Compiler.* under ThreadSanitizer (154 cases, among
    them ModulePartitions' split on 3 and 8 threads and ObjectCache's parallel WriteCachedObjects): no report.
  - *Code read.* BuildShare's threads share nothing writable: each reads the same const bitcode string into an
    LLVMContext of its own and writes its own slots of `out`. Each partition PartitionBitcode writes owns its context
    (`PartitionEmitter::m_Context`); the keyed modules (reflection image, host table) are built from data in a context of
    their own.
  - *The machine.* Kernel bugchecks 0x1A MEMORY_MANAGEMENT five times (11-15 0x61948, 11-17 0x41790, 11-22 0x41792, 11-24
    22:09 0x41790, 11-24 23:26 0x5100) and 0x133 once (11-22): corrupted page tables and PFN lists in the kernel, which no
    user program can cause. The fatal WHEA record (WHEA-Logger 1, 11-22 17:09 UTC) is an NMI (notify type
    5BAD89FF-B7E6-42C9-814A-CF2485D6E98A) with a processor-generic section of unknown error type on an AMD family 19h
    model 61h (Ryzen 7 7800X3D) and an NMI section with port 61h = 0x2C (no parity or I/O-check bit: the firmware did not
    name the source). 11-25 00:35: Memory-Diagnostic-Task-Handler 1001, "Windows removed bad memory regions from this PC".
    C4LSP.exe died with 0xc000001d (illegal instruction) twice at different offsets (11-24 21:35, 21:46) among ten access
    violations from 20:56 to 22:01, just before the 22:09 bugcheck; Edge and WebView2 (139 reports since 11-24 23:38) fault
    at msedge_elf.dll+0x133cac, a `js` that is the same in the file and in its mapped image (both installed copies, same
    SHA-256): not a bad page of the file - the processor, or code written there at run time. BUG-435 (closed) was seen on
    11-15, the day of the first bugcheck. Board: Gigabyte B650 GAMING X AX, BIOS F31 (2024-08-13); memory 2x G.Skill
    F5-6400J3239G32G at 4800 MT/s, 1.10 V (JEDEC, not EXPO). C:\Windows\MEMORY.DMP (11-24 23:26) needs an administrator.
  - *For the owner:* MemTest86 from USB (4 passes, both DIMMs, then each alone) or Windows Memory Diagnostic set to
    Extended; the latest BIOS for the board (F31 is old: newer AGESA and microcode), optimized defaults, no PBO/Curve
    Optimizer offset or EXPO while testing; HWiNFO64 during a long build for CPU temperature (the 7800X3D's limit is 89 C),
    SoC voltage and WHEA errors; a per-core CPU test (OCCT, or Core Cycler with Prime95/y-cruncher), since an illegal
    instruction on valid code is typical of one unstable core; WinDbg `!analyze -v` on MEMORY.DMP; then `sfc /scannow`.
    Close this entry if the crashes stop on sound hardware; reopen the code side only with a crash that repeats there.

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
