# Test suites

The project rule (AGENTS.md, LongTermPlan item 2) is extremely thorough testing: every feature, every
way features combine, every error path, also across C/C++ interop, with errors that are useful and never
a compiler crash. No duplicate tests; a failing test is fixed, never deleted, commented out or weakened.

All runners expect the compiler at `bin/Development-windows-x64/C4/C4.exe` (build it first, see
[04-building-the-compiler.md](04-building-the-compiler.md)) and write scratch output under
`assets/build-output/<suite>/` (gitignored). Nothing test-related goes in the repo root.

`nuke gates` from the repository root (the folder above the C4 tree) is the one command that builds the compiler
and runs every gate suite in turn: C4Testing, TestCode (also with `-Incremental`), NegativeTest (`-O0` and `-O2`), IsolatedTest (`-O0` and
`-O2`), RuntimeTest, ScriptTest (`-O0` and `-O2`), ToolsTest and Bedrock (the steps are in `C4/nuke.toml`).
`nuke test` there runs C4Testing alone (`nuke test C4 -- --filter "Lexer.*"`).

---

## 1. Overview

| Suite | Location | What it proves | Run |
|---|---|---|---|
| C4Testing | `C4Testing/src/Tests/` | C++ unit tests of the compiler library: lexer, source manager, parser, ProgramModel, overloads, HIR lowering, in-process codegen | `nuke test` (repository root), or `python Frost/Build.py -test -c Development -a x64 -p <C4 root>` |
| TestCode (e2e) | `assets/TestCode/` | one large C4 program covering the language end to end, run at `-O0` and `-O2` | `assets/TestCode/RunTests.ps1 [-DebugInfo] [-Partitions n] [-Incremental]` |
| NegativeTest | `assets/NegativeTest/` | invalid programs fail with the exact, actionable diagnostic | `assets/NegativeTest/RunTests.ps1 [-Filter x] [-O2]` |
| IsolatedTest | `assets/IsolatedTest/` | one folder = one feature or one historical bug, including interop | `assets/IsolatedTest/RunTests.ps1 [-Filter x] [-O2] [-DebugInfo] [-Partitions n]` |
| RuntimeTest | `assets/RuntimeTest/` | small runtime behaviours (printf, struct-return ABI) compile and exit 0 | `assets/RuntimeTest/RunTests.ps1 [-Filter x]` |
| StressTest | `assets/StressTest/Dx12Game/` | a large DX12 + Win32 game program compiles clean | `assets/StressTest/RunTests.ps1 [-Run]` |
| ScaleTest | `assets/ScaleTest/` | a generated 60k-line, 401-file program (classes, templates, containers, strings, GC objects, cross-module calls) compiles at `-O0` and `-O2` and runs on several threads with every result matching the single-threaded one | `python assets/ScaleTest/run.py [--seconds n --threads n --modules n]` |
| ScriptTest | `assets/ScriptTest/` | scripting end to end: per case, a host built with `--script-host` compiles, loads, calls and unloads its scripts, at `-O0` and `-O2` | `assets/ScriptTest/RunTests.ps1 [-Filter x] [-O2]` |
| ToolsTest | `assets/ToolsTest/` | the install and a toolchain outside the repository, every compiler flag, `nuke toolchain install` / `nuke init` / `nuke run` | `assets/ToolsTest/RunTests.ps1` |
| Bedrock | `Bedrock/Tests/` | the default library, through the built-in `[test]` framework | `C4.exe Bedrock\Tests -o <out>.exe -test`, then run `<out>.exe` |
| DifferentialTest | `assets/DifferentialTest/` | random programs (every primitive type, operators, conversions, control flow, struct parameters and results, pointers) print the same as the same program compiled as C by clang, at `-O0` and `-O2` | `python assets/DifferentialTest/run.py [--start n --count n --size x]` |
| Benchmarks | `Benchmarks/gemm/`, `Benchmarks/math/` | generated-code performance vs clang C++ at `-O2` (integer GEMM; Bedrock Math vs GLM); `-Check` only builds and compares results | `Benchmarks/<name>/run-benchmark.ps1 [-Check]` |

---

## 2. C4Testing

- Frost UnitTesting framework; tests are discovered through Frost reflection.
- Layout mirrors the compiler subsystems: `Lexer/`, `SourceManager/`, `Core/`,
  `Parser/{Foundation,Expressions,Statements,Declarations,Integration}/`, `Semantic/` (ProgramModelTest,
  OverloadResolution, ModelParity, SimulatedLowering, ProgramModelEdgeCases, HIRLowering, CodeGenTest,
  IsolatedCorpusModelTest - the last one loads real `assets/IsolatedTest` files with the headers beside each;
  DebugInfoTest - the `-g` metadata of a lowered module; ConstEvaluatorTest - constant folding; ScopeGuardTest;
  AliasTagsTest - the strict-aliasing class of each access, D-64),
  `Compiler/` (the runtime objects, the error tracker, project sets, the object cache and the module split:
  every partition verifies, a definition is in one partition, private data lives with its user, and adding a
  function leaves the other partitions' bitcode byte for byte the same), `Protocol/` (the JSON values and framing of `C4Protocol`), `LSP/` (the language server, linked
  through `C4LSPLib`) and `DAP/` (the debug adapter, linked through `C4DAPLib`: its pieces, and sessions of
  programs it builds with `C4.exe -g`; those tests take up to 60 seconds, `TEST_FUNC_TIMEOUT`).
- Filter with `--filter "Lexer.*"`.
- The old IsoDriver end-to-end classes and the other tests written against the pre-architecture APIs were
  retired in B26: every case still meaningful is an `assets/IsolatedTest` or `assets/NegativeTest` folder
  (their reasons name the original test), and the rest is covered by the current classes.
- `-testall` reports a crash exit code during ReflectionTesting teardown even when green; read the
  `Total/Passed/Failed` lines.

---

## 3. TestCode (the e2e gate)

`assets/TestCode/` is a single program (`Main.cf`) built from about 96 `.cf` files in category folders:
`Core`, `Operators`, `OperatorOverloads`, `ControlFlow`, `Types`, `OOP`, `Parity`, plus `Framework/`
(`Assert.ch`: `Assert`, `AssertIntEquals`, `AssertFloatEquals`, ...).

`RunTests.ps1`:

1. cleans intermediates in the test folder;
2. builds the folder at the default `-O0` into `assets/build-output/testcode/Main.exe` and runs it;
3. rebuilds the same folder at `-O2` and runs it again.

Both runs must succeed with zero failed assertions. `-Incremental` builds both with `--incremental`, so the `-O2`
build optimizes each object alone (D-76); the gates run it as the step `e2e-incremental`. (The `-O2` build currently writes `Main_O2.*` into
the test folder itself; stray `Main_O2.exe/.ll/.obj` files there are build output, not sources.)

Adding a test:

1. Put `Feature.ch` (declares `void TestFeature();`) and `Feature.cf` in the right category folder.
2. `include "Category/Feature.ch"` in `Main.cf` and call `TestFeature();` from `main`.
3. Use the `Framework/Assert.ch` helpers.

---

## 4. NegativeTest

- One folder = one invalid program (`main.cf`).
- Every folder must have an entry in `NegativeTests.json`:

```text
"FolderName": {
    "expect": ["substring of the expected diagnostic"],
    "reason": "why this must be an error and what a good message says"
}
```

- The runner compiles each folder and requires compilation to **fail** with every expected substring.
  A folder that compiles, a message that drifted, or an unregistered folder are all failures.
- Substrings match literally (brackets, quotes and `?` included).
- The manifest's `reason` fields also record known weak diagnostics (for example
  `TestSetupMustBeStatic`, `NonConstArraySize`) that should get sharper messages.

---

## 5. IsolatedTest

- Categories: `Basics`, `Bedrock`, `Calls`, `ControlFlow`, `Diagnostics`, `Exceptions`, `Functions`, `Interop`,
  `Lambdas`, `Lifecycle`, `Macros`, `MemoryGC`, `Namespaces`, `OOP`, `Operators`, `Pointers`, `Reflection`,
  `Simd`, `Templates`, `TestingFramework`, `Threads`, `Types` (see `assets/IsolatedTest/README.md`).
- `Simd/` (D-62): one folder per intrinsic family (`LaneMath`, `Masks`, `Shuffles`, `Reductions`, `Memory`),
  `Casts`, `Bug250ScalarBroadcast`, `ReferencesToIntegerLanes`, `WideVectors` (256- and 512-bit vectors through calls,
  fields, globals, the heap and every container on the default target), `TargetMacros`, and the cases
  `ExpectedExit.json` builds for another processor: `WideVectorsAvx2` (`-march=x86-64-v3`: YMM returns, aligned
  AVX loads from containers, gathers), `FusedMultiplyAdd` (`-mfma`) and `NativeTarget` (`-march=native`, the
  widest vectors the machine has). The C4Testing corpus sweep models each file for the target its entry names.
- `Types/StrictAliasing*` (D-64): the punning strict aliasing allows (`Unions`: named and anonymous union members and
  an array member; `Bytes`: `uint8`, `int8`, `char8` and `memcpy`; `BitCasts`: `reinterpret_cast` between numbers)
  and a loop over pointers held in a struct (`StructPointers`, which also moves a pointer through a `float**`). The
  pointers come from volatile variables, so the optimizer has only the access types to go by: a wrong tag gives a
  stale value at `-O2`. `ExpectedIR.json` checks the tags.
- Each folder is compiled and run on its own. `main` returns the failed-assert count; exit 0 is required
  unless a manifest says otherwise. A program still running after 60 seconds is killed and fails its case.
- `-DebugInfo` builds every folder with `-g`, so the debug information is checked against the whole corpus
  (it must verify, link and change nothing the program does).
- `-Partitions <n>` (TestCode too) assembles every program into `<n>` objects (`--partitions`), so the parallel
  code generation path runs the whole corpus: run it when touching codegen, linkage or `ModulePartitions`.
- Manifests next to the runner:
  - `NegativeTests.json` - folders that must fail to compile, with expected diagnostics;
  - `ExpectedExit.json` - folders that must compile and exit with a given code (for example uncaught
    exceptions exit 1), with `mustContain` / `mustNotContain` output checks (literal substrings) and
    `compileFlags` (a `-march` there is also the target the corpus sweep models the file for);
  - `ExpectedIR.json` - folders whose emitted `out.ll` must contain every `irContains` substring (for
    features whose effect is the code itself, such as volatile accesses and thread_local storage);
  - `SkipTests.json` - environment-dependent folders (GPU, specific Windows SDK), reported as skipped.
- Some folders need `-test`, CBind, a DX12 GPU or the Windows SDK version hard-coded in the interop
  tests (10.0.26100.0).
- Every folder sits in a category (`README.md` lists them); a regression test names its bug in its first
  comment line.

Run one test by hand:

```bat
bin\Development-windows-x64\C4\C4.exe assets\IsolatedTest\Types\SwizzleLanguage -o assets\build-output\iso\SwizzleLanguage.exe
assets\build-output\iso\SwizzleLanguage.exe
```


---

## 6. RuntimeTest, ScriptTest, StressTest and ToolsTest

- `RuntimeTest/`: `PrintfTest`, `StructReturn`. Compile and run; exit 0 required.
- `ScriptTest/` ([runtime/04-scripting.md](../runtime/04-scripting.md)): one folder per case, `host/` (the program,
  built with `--script-host` together with `Common/scripttest.ch`, its checks and script loading) and `scripts/`
  (copied beside the built program). The host exits 0 when every check held; with `-O2` the host and its scripts
  are both `-O2`. Cases: `HostCalls`, `Classes`, `Templates`, `Callbacks`, `Exceptions`, `GC`, `Reflection`,
  `Lifecycle`, `ThreadLocals`, `Macros`, `Diagnostics`, `MultiFile`, `Reload`, `CrashIsolation`, `ScriptTests`,
  `Concurrency`, `Interop`, `WideVectors` (BUG-252: 256- and 512-bit vectors both ways across a baseline host and
  its script, which compiles for the host's target), `WideVectorsAvx2`. A case's `flags.txt` holds more compiler
  flags for its host (`WideVectorsAvx2` builds for `-march=x86-64-v3`, so its script may use `simd_fma`). Output in
  `assets/build-output/script/<O0|O2>/<case>/` (`run.log`).
- `StressTest/Dx12Game/`: `Dx12Game.cf` with `windows.ch`, `game.hpp`. The gate is a clean
  compile; `-Run` executes the interactive game and needs a DX12 GPU.
- `ToolsTest/` (each check builds its programs under `assets/build-output/toolstest/` or `%TEMP%/c4-toolstest/`):
  - `ShippedBedrockMirrorsSource`: no missing, extra or different file in `bin/.../C4/Bedrock`.
  - Flags: `DefineFlag`, `VersionFlag`, `UsageErrors` (no input, an unknown option, a missing value: exit 2),
    `HelpListsEveryOption`, `OutputFlags` (`-c`, `-bc`, `--int-dir`), `GcStatsFlag`, `IncludeProject` (`-I`
    compiles the folder it names), `LinkFlags` (`-l`, `-L`).
  - Install: `InstallOutsideRepository` (a copy of the install in `%TEMP%` builds and runs a hello world and an
    `extern "c"` program with its own toolchain), `HomeVariable` (a bare `C4.exe` names its missing install;
    `C4_HOME` points it at one).
  - `NukeToolchainProject` ([02-nuke.md](02-nuke.md)): `MakeToolchain.py` into a scratch folder, `nuke toolchain
    install` into a scratch store (`NUKE_TOOLCHAINS`), `nuke init` outside the repository pinned to that version,
    then `nuke run` (intermediates in `build/int`); a module with its own define (it reaches only the module) and
    `links = ["winmm.lib"]` (they reach the program); exit codes; and a failed build (`nuke build` fails, and
    `nuke run` neither succeeds nor runs the old executable).
  - Projects ([language/15-projects.md](../language/15-projects.md)): `ProjectDefines` (each project's
    defines; at `-O2` the engine function is inlined into the game) and `ProjectBoundaries` (an include and a
    declaration across the boundary, an ambiguous path, a cycle, a misspelled key, inputs with `--projects`).
  - `DebugInfoFlag`: `-g` writes `app.pdb`, whose symbols (read with `llvm-pdbutil`) hold the function, its
    parameter and locals, the global and the lines of `main.cf`; a build without `-g` writes none.
  - `FrostStaleObjects`: Frost removes the objects of a source that is gone (never a generated source's or
    `linker.args`), and two sources with one base name are an error.
  - Building in any folder (`FrostProbes.py` drives Frost on generated projects): `FrostLongLink` (a link whose
    object paths pass the command-line limit goes through a response file), `FrostProjectName` (a project in a
    folder that is no identifier takes the name its `Build.py` gives), `FrostBuildOrder` (early projects build after
    the early projects they link), `FrostFailedCompile` (a compile that fails discards the object and dependency
    file it may have written), and `NoIgnoredSources` (no source outside the build folders, installed packages
    and Frost's vendored toolchain is hidden by `.gitignore`).
  - `LspStdio`: `StdioClient.py` drives `C4LSP.exe` over real stdio: `initialize`, a malformed frame it drops,
    `shutdown` and `exit` (exit code 0).
  - `DapAdapter`: `StdioClient.py` drives `C4DAP.exe` over real stdio through a session of a `-g` program: its
    breakpoint, stack and locals, its output and exit code, `disconnect`.
  - `TargetFlags` (D-62): `-march=x86-64-v3 -mno-fma` gives the program its `SIMD_*` macros, `-march=znver4` is on
    every function in the IR, an unknown CPU or feature is a usage error, and a foreign C source and the header
    CBind binds both see the same processor (`__AVX2__`).
  - `StrictAliasingFlag` (D-64): a default build's IR has alias tags and a `-fno-strict-aliasing` one has none; a
    pointer pun the optimizer cannot see through reads what was written at `-O2 -fno-strict-aliasing`.
  - `Partitions` (roadmap 5.1): a program with a `static` global that a foreign C global shares the name of,
    file-scope assembly, virtual calls, template instances, exceptions, `thread_local`, reflection and a global with
    a create runs when assembled into 4 objects, into 3 with `-g` (its `.pdb` has the functions), into 3 on one
    thread at `-O2`, and by default; a one-line program is one object; `-c` writes one; bad `-j` / `--partitions`
    counts and `-c --partitions 2` are usage errors.
  - `ObjectCache` (roadmap 5.3): an unchanged rebuild reuses every object, editing one function reassembles only its
    partitions and the program shows the edit (no stale object is linked), an `-O2` build after `-O0` reuses none,
    the cache keeps only the latest build's objects, and `-c` makes no cache.
  - `IncrementalBuild` (D-76): at `-O2 --incremental` an unchanged rebuild reuses every object, editing one function
    reoptimizes only some and the program shows the edit, `-ftime` reports `optimize+codegen`, a whole-program build
    afterwards reuses none, and `-c --incremental` is a usage error.

Collector tests live in IsolatedTest `MemoryGC/` (liveness, deep lists, large objects, interior pointers,
pins, hooks, finalizer order and exceptions, static roots, noscan, pacing, `delete`, collections while several
threads allocate, thread_local roots, pools, concurrent marking). The gate step `gc-concurrent`
(`assets/ConcurrentGcTest/run.py`) runs Bedrock's tests, IsolatedTest `MemoryGC` and the GC benchmark's check again
with every automatic collection a concurrent cycle verified against a fresh mark (`C4_GC_CONCURRENT=always`,
`C4_GC_VERIFY=1`) and a 64 KiB trigger (`C4_GC_MIN_TRIGGER`). A test that asserts an
object *was* collected allows a few survivors: a conservative collector may keep an object whose address is
still in a register or a dead stack slot.

---

### ScaleTest

`generate.py` writes the program under `assets/build-output/scale/src`: 200 modules (`m<i>.ch` / `m<i>.cf`), each a
record type, a ledger class over `Array` and `HashMap<String, int64>`, a template used at two types, a chain of
GC-allocated holders, twelve arithmetic steps and a `Work` that calls the module before it, and a `main.cf` that
runs every module's `Work` on `SCALE_THREADS` threads for `SCALE_SECONDS`, checking each result against the one a
single thread computed first (the exit code counts the wrong ones). `run.py` generates it, compiles it with the
repository's `C4.exe` at each level, times the compile and runs it. It is not in `nuke gates` (it takes minutes);
run it after changes to the runtime's threading, the GC, or code generation at scale. Recorded 2026-10-20: 59646
lines in 401 files, `-O0` in 4.4 s and `-O2` in 15.0 s with `-j 3`; 60 s on 4 threads, 18939 runs at `-O0` and
29849 at `-O2`, none wrong.

## 7. Bedrock tests

`Bedrock/Tests/*_tests.cf` (37 files, 340 tests; also built for `-O2 -march=x86-64-v3` and `-march=native`). See
[library/bedrock.md](../library/bedrock.md).
The exit code of the `-test` executable is the failed-assertion count.

---

## 8. Benchmarks

Both benchmarks build the C4 side through C4.exe and the C++ side with the vendored clang, at `-O2` with the
same target flags; `-Check` builds them and compares results (a gate), and without it they are timed.

`Benchmarks/gemm/`: a 512^3 integer GEMM in C4 (`gemm.cf`) and C++ (`gemm.cpp`), timed end to end, median
of 7 runs.

`Benchmarks/math/` (`run-benchmark.ps1`), two suites timed in process over 4096 vectors x 400 repeats, each in
a folder of its own (C4.exe compiles a file with every source under its folder); GLM 0.9.9 is vendored in
`Benchmarks/math/glm/` (MIT). The checksums must agree to 1e-4 relative; the times are the median of 5 runs.

- `per-vector/`: seven per-frame kernels: building TRS matrices, matrix products, inverses, point transforms,
  quaternion rotation, slerp and normalized crosses. `math.cf` uses Bedrock Math; `math.cpp` the same program over
  GLM, built plain and with GLM's SSE paths (`GLM_FORCE_INTRINSICS` over aligned types). `-March <cpu>` builds all
  three for that processor.
- `batch/`: six kernels over eight vectors at a time, always for `-march=x86-64-v3`: point transforms (also through
  pointers to the arrays held in a struct, `batch_pointers`), quaternion rotation, normalized crosses, a particle step (gravity, motion, a bounce and walls: fused multiply-adds, masks,
  min / max) and distances. `batch.cf` keeps the vectors in Bedrock's `FVector3x8` batches; `batch.cpp` is built as
  hand-written AVX2 intrinsics over the same structure-of-arrays batches (`BATCH_AVX2`) and as the per-vector GLM
  loops a GLM program would write.

Baselines (2026-10-19, ms; ratio < 1 means C4 is faster), per-vector at the default target and at `-March
x86-64-v3`:

| Kernel | C4 | GLM | GLM SSE | C4/GLM | C4/SSE | C4 v3 | GLM v3 | SSE v3 | C4/SSE v3 |
|---|---|---|---|---|---|---|---|---|---|
| build_transforms | 45.4 | 63.1 | 46.8 | 0.72 | 0.97 | 43.3 | 59.0 | 46.1 | 0.94 |
| invert_matrices | 9.6 | 27.0 | 9.3 | 0.36 | 1.03 | 9.6 | 22.9 | 9.2 | 1.04 |
| multiply_matrices | 4.9 | 11.3 | 5.3 | 0.43 | 0.91 | 3.9 | 8.7 | 4.3 | 0.91 |
| normalize_crosses | 3.8 | 3.5 | 3.6 | 1.09 | 1.05 | 3.3 | 3.3 | 3.5 | 0.93 |
| rotate_vectors | 2.6 | 3.5 | 4.0 | 0.75 | 0.67 | 2.5 | 3.0 | 3.4 | 0.72 |
| slerp_rotations | 24.9 | 25.0 | 24.8 | 1.00 | 1.00 | 25.2 | 25.0 | 24.4 | 1.03 |
| transform_points | 1.3 | 2.1 | 1.5 | 0.59 | 0.87 | 1.1 | 2.2 | 1.5 | 0.78 |
| gemm (end to end) | 234 | 237 (C++) | | 0.99 | | | | | |

The matrix inverse took 12.1 ms (1.28x GLM's SSE version) before B39 shuffled it the way GLM does.

| Batch kernel (x86-64-v3) | C4 | GLM | AVX2 | C4/GLM | C4/AVX2 |
|---|---|---|---|---|---|
| batch_distance | 0.33 | 0.52 | 0.38 | 0.63 | 0.85 |
| batch_integrate | 0.43 | 0.73 | 0.47 | 0.59 | 0.92 |
| batch_normalize | 0.55 | 1.42 | 0.55 | 0.38 | 0.99 |
| batch_rotate | 0.57 | 0.99 | 0.54 | 0.57 | 1.04 |
| batch_transform | 0.51 | 0.87 | 0.50 | 0.58 | 1.01 |
| batch_pointers (2026-10-20) | 0.49 | 2.14 | 0.55 | 0.23 | 0.89 |

`batch_pointers` reads its arrays through pointers held in a struct. With strict aliasing (D-64) a vector store
cannot change those pointers, so the loop keeps them in registers and runs as fast as `batch_transform` (0.41 ms
against 0.46 ms with `-fno-strict-aliasing`, measured back to back). The AVX2 version reloads them after every
store, since clang lets a `__m256` alias anything; the GLM loop is slower for the same reason through `glm::vec3`
stores into a pointer it must reload.

`Benchmarks/gc/` (B32): a live world of entities (each with a chain of components and a parent link) that stays
reachable while every frame allocates short-lived garbage and replaces a share of the world; run on the main
thread alone and with the frame's churn as job-system jobs, each with stop-the-world collections
(`C4_GC_CONCURRENT=never`) and with the default, which marks this heap concurrently (D-79). It prints frame-time percentiles, collections, the
average and worst pause, the GC's share of the time and allocations per second; `-Check` runs a small world both
ways and requires the same checksum.

Baselines (2026-10-19; 200,000 entities, about 52 MiB live, 600 frames of 20,000 garbage objects and 200
replacements; 16 logical processors):

| Mode | Avg frame | p99 frame | Avg pause | Worst pause | Allocations/s |
|---|---|---|---|---|---|
| Alone, marking on one thread (before B32) | 0.84 ms | 20.6 ms | 21.5 ms | 31.5 ms | 25 M |
| Alone, parallel marking (8 markers) | 0.55 ms | 6.4 ms | 6.2 ms | 7.4 ms | 38 M |
| Jobs (15 workers), parallel marking | 0.19 ms | 6.5 ms | 6.5 ms | 8.3 ms | 114 M |

Marking is the whole pause (the sweep takes about 0.1 ms, stopping 16 threads about 0.2 ms). It scales by
marker count as 19.3, 9.5, 7.1, 5.7 and 5.1 ms for 1, 2, 4, 8 and 12 markers; more markers than processors
oversubscribe (16 markers: a 31 ms worst pause), so the default stops at 8.

Concurrent marking (B64, 2026-10-20; the pause columns add up a cycle's stops, each under about 1 ms):

| Mode | Avg frame | p99 frame | Worst frame | Pauses per cycle | Live at the end |
|---|---|---|---|---|---|
| Alone, stop-the-world | 0.54 ms | 5.2 ms | 7.5 ms | 4.9 ms | 53 MiB |
| Alone, concurrent | 0.54 ms | 1.1 ms | 1.6 ms | 1.7 ms in 4 stops | 80 MiB |
| Jobs (15 workers), stop-the-world | 0.17 ms | 4.8 ms | 6.6 ms | 5.0 ms | 51 MiB |
| Jobs (15 workers), concurrent | 0.18 ms | 1.3 ms | 6.1 ms | 5.4 ms in 5 stops | 114 MiB |

With 15 workers the marking helpers compete for the 16 processors, which shows in the worst frame.

`Benchmarks/compile/` (B32): C4.exe end to end on a one-line program, the Bedrock tests and TestCode, at `-O0`
and `-O2`, the median of 3 builds with each build's `--time-phases` breakdown; `-Check` builds each once.

Baselines (2026-10-20, after B46 and B47, 4 code generation threads; ms, phases in seconds; 2026-10-19 before them
in parentheses):

| Program | -O0 | -O2 | Where -O0 goes | Where -O2 goes |
|---|---|---|---|---|
| hello | 97 (142) | 96 (147) | runtime and link 0.08 | runtime and link 0.08 |
| Bedrock tests (31 files) | 698 (1,235) | 3,178 (4,219) | lower 0.12, codegen 0.36, link 0.09 | lower 0.13, optimize 1.95, codegen 0.87, link 0.09 |
| TestCode | 360 (491) | 605 (854) | parse 0.04, lower 0.03, codegen 0.12, link 0.09 | optimize 0.23, codegen 0.13, link 0.09 |

The front end is a small part of every build. The object is now emitted in process (B46) and a large module is
assembled in parallel partitions (B47); at `-O2` the single-threaded LLVM optimizer is now most of the time, and at
`-O0` code generation. Caching the objects of unchanged partitions is the next compile-time item (roadmap 5.3).

Script compiles (2026-10-21, `Host::Compile` of IED's 78-line Go plugin against the IED server's 4.9 MB interface,
compile and link; before in parentheses): `-O2` 0.26 s (4.6 s the first time, 2.3 s after), `-O0` 0.20 s (1.75 s).
The time went to rebuilding the reflection registry's name index for every function a script registers, a
file-system check of the interface's virtual `//c4i` folders on every include lookup (a network path to Windows),
reading and optimizing every host inline body the script reached transitively (4,783 bodies; now two calls deep,
read lazily, none at `-O0`), and full code generation for `-O0` scripts (now the fast instruction selector).
With the compiled-script cache (`Host::UseCache`, docs/runtime/04 section 4.6) the same script loads in 4 ms.

`Benchmarks/bedrock/` (`run-benchmark.ps1`): Bedrock against the C++ standard library at `-O2`, the same kernels
over the same pseudo-random data in `bedrock.cf` and `bedrock.cpp`, each timed in process (median of 5 runs) with a
checksum both sides must print alike: `HashMap` (1M `int64` keys: insert, hits, misses, removes; 200k text keys:
insert and five rounds of hits) against `std::unordered_map`, `Array::Push` against `std::vector::push_back`,
`Sort` against `std::sort` (2M `int32`, and 2M 8-byte records by key through a lambda), and number text against `std::to_chars` / `std::from_chars` (1M integers
and doubles). `-Check` builds both and compares the checksums once (gate item 11).

Baselines (2026-10-21, ms; ratio < 1 means Bedrock is faster):

| Kernel | Bedrock | C++ | Bedrock/C++ |
|---|---|---|---|
| map_int_insert | 22.4 | 98.5 | 0.23 |
| map_int_find_hit | 7.4 | 24.9 | 0.30 |
| map_int_find_miss | 3.4 | 27.0 | 0.13 |
| map_int_remove | 3.2 | 38.6 | 0.08 |
| map_text_insert | 22.3 | 40.9 | 0.55 |
| map_text_find | 32.1 | 50.9 | 0.63 |
| array_push_sum | 16.5 | 21.9 | 0.75 |
| sort_int32 | 9.6 | 106.0 | 0.09 |
| sort_records | 43.5 | 108.4 | 0.40 |
| text_append_int | 12.0 | 13.3 | 0.90 |
| text_format_double | 36.0 | 36.5 | 0.99 |
| text_parse_double | 21.3 | 73.8 | 0.29 |
| json_write (C4 only, 10 MB) | 21.4 | - | - |
| json_read (C4 only, 10 MB) | 27.7 | - | - |

Before the Swiss-style table and the wyhash-style byte hash (linear probing over a separate state byte per slot,
FNV-1a), the map kernels took 32.4, 8.9, 11.5, 3.3, 41.4 and 41.1 ms. Before the radix sort `sort_int32` took 104.3 ms (introsort), and the records about as long as `std::sort` before
pdqsort with block partitions;
before the Eisel-Lemire fast path `text_parse_double` took 77.3 ms (`from_chars`); before the DOM's single child block
and string views of the source `json_read` took 72.2 ms and `json_write` 28.4 ms. The shortest double form took
4,851 ms for the million doubles before the number text moved to `<charconv>`
(it printed and read back up to 17 times per number), and the integers 31.4 ms before they were written two digits
at a time (14.9 ms before one call wrote the sign and digits of a decimal, and a const string pointer was read-only
data the loop's loads of could fold).

---

## 9. Gates

A change is done only when all of these pass. `nuke gates` from the repository root runs items 1 to 10 (with
NegativeTest and IsolatedTest also at `-O2`) except StressTest and IsolatedTest `-DebugInfo`; run those and the
benchmarks (item 11) on their own.

1. The compiler builds with no swallowed errors (grep the log, see the traps list).
2. C4Testing: `nuke test` (repository root), or `python Frost/Build.py -test -c Development -a x64 -p <C4 root>`.
3. TestCode at `-O0` and `-O2`: `assets/TestCode/RunTests.ps1`.
4. NegativeTest: `assets/NegativeTest/RunTests.ps1`.
5. IsolatedTest: `assets/IsolatedTest/RunTests.ps1` (and `-DebugInfo` when touching codegen or debug info).
6. Bedrock: `C4.exe Bedrock\Tests -test` build + run, exit 0.
7. RuntimeTest and StressTest (compile) when touching the runtime, ABI or interop.
8. ScriptTest at `-O0` and `-O2`: `assets/ScriptTest/RunTests.ps1` and `-O2`.
9. ToolsTest: `assets/ToolsTest/RunTests.ps1`.
10. DifferentialTest: `python assets/DifferentialTest/run.py` (150 seeds at `-O0` and `-O2`); after codegen or
   conversion changes also a long run (`--start 1000 --count 5000 --size 1.5`).
11. Benchmarks build and agree with C++: `Benchmarks/gemm/run-benchmark.ps1 -Check` and
   `Benchmarks/math/run-benchmark.ps1 -Check`; the GC benchmark computes the same world alone and with workers
   (`Benchmarks/gc/run-benchmark.ps1 -Check`); the compile benchmark's programs build
   (`Benchmarks/compile/run-benchmark.ps1 -Check`); Bedrock's kernels print what the C++ standard library's do
   (`Benchmarks/bedrock/run-benchmark.ps1 -Check`, also a `nuke gates` step).

---

## 10. Conventions

- Every new behaviour lands with tests for the happy path, edge cases (empty, boundary values) and
  error paths. Every new diagnostic gets a NegativeTest.
- One concept per test folder or file; name it after what it covers, never `MoreTests` or `Test2`.
- C++ unit tests mirror the source subsystem layout (`C4Testing/src/Tests/<Subsystem>/`).
- Tests are independent: each sets up its own state; no order dependence or shared mutable fixtures.
- A bug fix starts with a failing repro (usually an IsolatedTest folder), then the fix, then the test
  is kept as a regression guard.
- Scratch, throwaway repros and logs go to `assets/build-output/` (ignored by git); a repro worth keeping becomes
  an IsolatedTest folder.

### Coverage gaps worth filling

Macro `#if` forms, `goto`, throwing objects, lambdas, multiple interfaces, GC hooks (root enumerators,
collection callbacks, pin, disable), reflection groups/variables/functions/unload, and feature
interactions with interop. See [status/implementation-status.md](../status/implementation-status.md).
