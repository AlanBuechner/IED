# Roadmap and open questions

Where C4 is going. Section 1 tracks the long-term plan (`LongTermPlan.txt`), section 2 the nearer
backlog, section 3 the design questions that still need a decision, section 4 ideas from the retired
docs that were never adopted, section 5 the optimization and fast-iteration tasks, and section 6
compile-time evaluation.

Bugs in existing features, including language rules the compiler does not enforce yet, are tracked
in [known-bugs.md](known-bugs.md), not here.

---

## 1. Long-term plan

The project owner's plan, in order. Status is as of 2026-10-19.

| # | Item | Status | Notes |
|---|---|---|---|
| 1 | **Implement the C4 language** | **Partial** | See the sub-items below and [implementation-status.md](implementation-status.md) |
| 1a | Built-in SSE/AVX support | **Done** (B39, D-62) | `packed` lane views and `packed T[N]` values lower to LLVM vectors at every width up to 512 bits; `-march` / `-m<feature>` pick the processor (with `SIMD_*` macros), for the program, its foreign sources and its scripts; the `simd_*` intrinsics (lane math, FMA, masks, shuffles, reductions, gathers). Bedrock Math runs on them and adds 8-wide batches, which match hand-written AVX2 in `Benchmarks/math/`. [vectors](../language/11-vectors-and-swizzling.md#8-the-simd-target), [builtins](../language/14-builtins.md#11-simd-intrinsics) |
| 1b | `asm` blocks | **Done** (B45) | `asm` statements with named operands (`in`, `out`, `inout`, `const`), register classes (`reg`, `xmm`, `ymm`, `zmm`, explicit registers), options and clobbers, modelled on Rust's `asm!`; file-scope `asm`. [inline assembly](../language/16-inline-assembly.md) |
| 1c | HLSL-like swizzling as a generic feature | **Done** (B42) | `v.xz = {1, 1}`, `v3.zx = v2.xy`, broadcast compound ops, and the whole-object form `v3.zx = v2` (a `Vec2` fills a run of its lanes and combines with one in operators). Math operators (`v1 += v2`, `v1 + v2`) are written by the library; performance validation is under 1a. [vectors](../language/11-vectors-and-swizzling.md#2-lane-access-and-swizzle-runs) |
| 1d | More macro support | **Done** (B4) | `#if` expressions, `#` / `##`, `\` continuation, calls across lines, `#error` / `#warning` / `#pragma`, comments that span lines, `-D`. [preprocessor](../language/02-preprocessor.md) |
| 1e | Default macros (`PLATFORM_WINDOWS`, `PLATFORM_LINUX`, `TEST`, ...) | **Done** (B4) | `TEST` under `-test`, `PLATFORM_WINDOWS`, `ARCH_X64`, `C4_VERSION`, `__FILE__`, `__LINE__`; `PLATFORM_LINUX` comes with the Linux port. [preprocessor](../language/02-preprocessor.md#26-predefined-macros) |
| 1f | Multiple return values | **Done** (A9, D-34) | `[float, float, bool] F() { return [1.0f, 2.2f, true]; }`, `[float a, float b, bool ok] = F();`, `[a, float b, ok] = F();`, `[a, _] = F();`. [functions](../language/07-functions-and-lambdas.md#79-multiple-return-values) |
| 1g | Built-in threading (join, promise, condition variable, work stealing) | **Done** (B29, D-48, D-60, D-61) | The collector stops and scans every attached thread, with per-thread allocation caches; atomic intrinsics; thread_local of any type; Bedrock's `Threading::` (atomics, threads, lock-free mutex and condition variable, wait groups, futures, a latency-first job system). [Bedrock section 10](../library/bedrock.md#10-threading-threading) |
| 1h | Remove lvalue/rvalue and explicit move semantics | **Done** (A3) | D-05 / D-06: copies by operator or memberwise, the compiler relocates temporaries, returns and last uses, `take(x)` moves explicitly; no `T&&`, no move operator. [lifecycle](../language/06-lifecycle-and-operators.md) |
| 2 | **Extensive testing** of every feature, every combination, every error | **Partial** | Thousands of tests per feature is the bar; no duplicate coverage; everything also through C++ interop; errors must be useful; perf parity with C/C++ validated. [test suites](../tools/05-test-suites.md) |
| 3 | **Default library** | **Partial** | Done (B20a, D-58): one-level namespaces; `Core::Array` (was Vector), `Core::HashMap` (keys through `operator_hash` / `==` by `KeyTraits`), `Core::Queue`, `Core::Stack`, `Core::Sort`, UTF-8 `Core::StringView` / `String` / `StringBuilder`, `Core::Format`, `Core::List`, `FS::File` / `FS::Path` / `FS::Directory`, and the `Serialization` DOM with JSON and YAML readers and writers, a binary writer / reader and reflection integration (B20b, B20c), and the `Math` library (3a). `Threading::` (B29), `Platform::` (B40: virtual memory, processors and memory, the executable's path, environment variables, libraries, wall-clock and calendar time). Missing: data streaming (decode large map files while they are still being parsed, for faster load times). [Bedrock](../library/bedrock.md) NOTE: this should be a highly optimized and fleshed out default library. single namespace deep like Reflection:: Math:: Platform::(os level interaction like virtual memory allocations and more) Threading:: FS::(this is the file system FS::Path FS::Directory etc) or anything that is not part of a larger system should be in the Core:: namespace |
| 3a | Math library | **Done** (B20 step 6) | `Math::` in `Bedrock/Math/` ([Bedrock section 9](../library/bedrock.md#9-math-math-include-mathch)): packed SIMD vectors with swizzling, `Mat2/3/4`, `Quat`, transforms and cameras, one consistent YXZ Euler convention; at or ahead of GLM on most kernels of `Benchmarks/math/`; the vector and matrix functions use the SIMD intrinsics and `batch.ch` works on 8 (or 4, 16) vectors at a time (B39). Original note: Vectors (with swizzling), matrices, quaternions, all the functions to use them, SSE/AVX optimized, left-handed, 0-1 NDC, parity with Frost's math library / GLM or better. Lives in `Math::`, opt-in through `include "math.ch"` (D-46). we have an example of this in a game engine's math library though we should flesh it out more with more optimizations |
| 4 | **Reflection rework** | **Done** (B19) | Types, fields, methods, enums, variables, functions, groups; invoke through per-function thunks (`Invoke`, typed `Call`), field `Get` / `Set`, per-type static listing, parameter names and flags, reflected annotations with constant arguments, fully qualified identity for template instances and namespaced types, hot-reload unload with listeners. Unions are not registered yet. [reflection](../runtime/02-reflection.md) |
| 5 | **GC audit** | **Partial** | Engine hooks exist. Missing: much more testing and performance testing; hooks for custom allocators to describe each object inside one allocation and decide whether those objects get collected. [memory and GC](../runtime/01-memory-and-gc.md) |
| 6 | **Built-in scripting with LLVM ORCv2** | **Done** (B30) | Scripts share the same runtime, type system, reflection registry and GC as compiled code, compile against the host's `.c4i` interface alone, hot reload, and a crashing script does not take down the engine ([runtime/04-scripting.md](../runtime/04-scripting.md)). Debugging scripts (JIT code has no PDB) is later work. |
| 7 | **More default library** - file IO, sockets/networking, HTTP server | **Partial** | `FS::File` done (64-bit positions, relative seek); directories planned (B20). Sockets and HTTP planned. |
| 7b | C4 LSP | **Implemented** | [editor](../tools/03-editor-lsp-dap.md) |
| 8 | **C4-only build system** | **Implemented** | Nuke, at the repository root (`../Nuke`), with versioned C4 toolchains. [Nuke](../tools/02-nuke.md) |
| 9 | **Self-hosting** (C4 compiler written in C4) | **Planned** | The proof that the language and compiler are stable. |
| 10 | **DAP debugger** | **Done** (B28) | `-g` writes CodeView and a `.pdb`; `C4DAP` debugs C4 programs from any DAP client: breakpoints, stepping, the stack, values by their types, `evaluate`, exceptions, pause. [editor](../tools/03-editor-lsp-dap.md#3-c4dap-debugger) |
| 11 | **Linux support** | **Planned** | GC runtime (VirtualAlloc, TEB), linker invocation and ABI are Windows-only today. |
| 12 | **Port a game engine to C4** | **Planned** | Skipped for now: a port already exists in a separate project, and the owner wants to use this item to update the engine design. |

---

## 2. Nearer backlog

The backlog is now the work-package list in [plan.md](plan.md): every feature that is part of the
design but not built has a work package there with steps, files and tests. Add new backlog items to
the plan, not here.

---

## 3. Design questions (decided 2026-10-18)

Every question that used to be listed here has been decided. The decisions are in the table in
[plan.md section 3](plan.md#3-design-decisions) and in the normative text of each owning page.

| Former question | Decision |
|---|---|
| 3.1 Copy, move and value categories | D-05, D-06: copy by operator or memberwise, compiler relocation for temporaries, returns and last uses, `take(x)`; no `T&&`, no move operator |
| 3.2 Destructor name | D-03: only `[operator("delete")]`; `"destroy"` / `"finalizer"` are errors |
| 3.3 `int` and other alias names | D-01, D-02: `int` = `int32`, `uint` = `uint32`, `char` = `char8` |
| 3.4 Where the math library lives | D-46: `Math::` (one level), opt-in through `include "math.ch"`; the rest of Bedrock is in one-level namespaces too (`Core::`, `FS::`, `Serialization::`, `Reflection::`, `GC::`, `Platform::` and `Threading::`), D-58 |
| 3.5 Operator precedence | D-27: C++ table, `is`/`as` relational, `**` with the Python rule |
| 3.6 Visibility sections | D-28: labels require matching inline modifiers; full access checks |
| 3.7 Lambda captures | D-22: C++ captures, closure types, `func<R(A)>`, call operator |
| 3.8 Base-class construction and destruction | D-07: C++ order; base create with arguments is the first statement of a create |
| 3.9 Template instantiation scope | D-16: on demand, like C++ |
| 3.10 Scripting model | D-49: ORCv2 over ordinary `.cf` files, crash isolation and an include allow-list |
| 3.11 Threading and the GC | D-48: suspend-and-scan with per-thread allocation caches, parallel marking (B32). Next: a concurrent mark so a pause no longer grows with the live heap (about 6 ms per 50 MiB today) |
| 3.12 The `loop` keyword | D-04: removed |

Still open, for the project owner: whether the GC becomes optional per program, and whether the test
framework grows parameterized tests and benchmarks. Neither blocks a work package.

---

## 4. Ideas from the retired docs that were not adopted

The old `docs/lang-design/` pages were partly speculative. These ideas appeared there but are not part
of the current design. Treat them as unconfirmed; adopt one only by moving it into section 2 or 3.

| Idea | Status |
|---|---|
| Hybrid ARC + concurrent generational GC, `T^` references, `[GC]` / `[Nogc]`, manual zones | Replaced by the Go-style collector |
| Tiered frame arenas, young/old generations, precise stack maps | Possible future GC work; not committed |
| `delegate<...>` | Replaced by `funcptr<...>` |
| Nullable `T?`, `tuple<...>`, `auto`, `import`, `println` | Not part of C4 (multiple return values replace tuples) |
| "Basis" library namespace, StringView / HashSet / Deque / Span / Optional / Result / BitSet / Random / Time | The library is Bedrock; individual types can be proposed into section 2 |
| `[Derive(...)]`, `[CTGen]`, observable properties, MVVM bindings | Not planned |
| LLI interpreter and `.c4s` files | Replaced by the ORCv2 plan (3.10) |
| `[Test]` capitalized, `*_test.cf` naming, parameterized tests, benchmarks, mocking | Testing uses `[test("group")]` in `src/tests/`; parameterized tests and benchmarks are unconfirmed |
| Private header symbols visible only to the paired `.cf` | Not part of the program model |
| A dedicated `swizzle Vec4 { ... }` declaration | Replaced by `packed` / `unpacked` union views |
| Default constructors disabled by default | Contradicted: value locals run `create` automatically |

---

## 5. Optimization and fast iteration (Planned)

Performance work is spread across the plan today (B32 covers benchmarking, 1a SIMD parity, item 5 GC
performance), but there is no single task that owns making C4 fast to build and fast to run. This
section closes that gap. It is a work package in its own right; when it is scoped into steps with
files and tests it moves into [plan.md](plan.md) like every other package.

| # | Task | Status | Notes |
|---|---|---|---|
| 5.1 | **Build-time optimization** | **Planned** | Baselines are recorded (`Benchmarks/compile/`, B32): the front end is under 0.1 s even for the Bedrock tests, while the LLVM optimizer and object emission (`clang -c` on the written `.ll`) take 3.6 of 4.2 s at `-O2`; next are in-process object emission without the text IR round trip and splitting the module for parallel code generation. The compiler and Nuke build are currently single-shot: one program, one compile, no cache of parsed or lowered units. Profile where the time goes (parse, semantic analysis/`ConstEvaluator` fixed point, codegen, LLVM, link) with `-ci`, record baselines in [test suites](../tools/05-test-suites.md), then attack the hot phases: cheaper incremental model updates, parallelizing independent files/units where the program model allows, and caching parse trees or HIR for unchanged inputs. Bedrock (compiled as part of every program) is the biggest fixed cost; it should not be re-parsed and re-lowered on every build. |
| 5.2 | **Runtime optimization** | **Planned** | Generated code must hold up against C/C++ at `-O3` (the bar set by long-term item 2). Cover: LLVM pipeline settings per config (`-O0`/`-O3`), inlining heuristics and LTO where the program model allows, GC pause and throughput under churn (long-term item 5), Bedrock math vs GLM/Frost parity (1a, B32), allocation patterns (no hidden copies; D-05/D-06 relocation already helps). Done: type-based alias information (B41, D-64: C++'s strict aliasing, `-fno-strict-aliasing`), so loops keep pointers held in memory in registers. Benchmarks with recorded baselines land in [test suites](../tools/05-test-suites.md) and gate regressions. |
| 5.3 | **Incremental / iterative builds** | **Planned** | Rebuild only what changed, for very fast edit-compile-run loops during development. The design deliberately trades some inlining for speed: unchanged units are recompiled to object/bc (or equivalent) artifacts and reused as-is, so cross-unit inlining of unchanged code is dropped in iterative mode; the full build keeps today's single-program inlining behavior. Concretely: per-unit compile + link step with dependency tracking (`deps.d` exists but only drives whole-project rebuilds), a stable unit ABI across builds (names/mangling, layout, vtable shape) so old objects stay valid, and Nuke driving the incremental pass while `nuke build` stays the authoritative full build. Target: a one-file edit rebuilds in well under a second once the first full build is done. |

---

## 6. Compile-time evaluation (Planned)

The language already folds constant expressions (`ConstEvaluator`: literals, arithmetic, `sizeof`,
folds, const/enum/template-parameter references; see [program model - Constant evaluation](../compiler/02-program-model.md#constant-evaluation)),
but user functions cannot run at compile time. Extending this is a planned direction:

- **`constexpr`-style function evaluation.** A `const` initializer (or any constant-expression
  position) may call a function marked as compile-time evaluable; the compiler runs it over already
  folded values and stores the result. Non-evaluable inputs are a clean error, not a fallback to
  runtime code.
- **Scope grows in stages.** First pure functions on scalars and value types with no allocation; then
  control flow (loops with bounded iteration), then container/array construction at compile time
  (e.g. building lookup tables or shader-like data). Each stage needs the `ConstEvaluator` to model
  that feature, so this is a sequence of small work packages, not one big one.
- **Interaction with the rest of the plan.** Template value arguments already require constant
  expressions, so CTFE immediately widens what templates can take (`Arr<BuildTable()>`); annotation
  arguments (D-42) get the same for free; and it is a prerequisite for generating data at compile
  time instead of shipping precomputed blobs.
- **Guards.** Evaluation must terminate: bounded iteration, no unbounded recursion, a step limit with
  a clear diagnostic. A function that cannot prove termination stays runtime-only.

