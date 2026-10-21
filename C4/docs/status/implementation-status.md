# Implementation status

What works today and what is partial, feature by feature. Individual bugs are tracked in
[known-bugs.md](known-bugs.md). Checked against the source at commit `804e028` (2026-10-17) plus the working
tree on 2026-10-18.

When you fix something on this page, delete its row here and remove the matching "Current
implementation" note on the feature's page. Planned work and open design questions live in
[roadmap.md](roadmap.md).

Last recorded gate results (2026-10-20, after B68): TestCode e2e 1505 assertions / 0 failures at `-O0` and `-O2`
(also with the program split into 8 partitions, and at `-O2 --incremental`); NegativeTest 647 at `-O0` and `-O2`; IsolatedTest 440 at `-O0` and
`-O2` (also 4 partitions with `-g`); RuntimeTest, ScriptTest (19 cases at `-O0` and `-O2`), ToolsTest (30 checks),
DifferentialTest (150 programs against clang), the concurrent-GC step (Bedrock, MemoryGC and the GC benchmark with verified concurrent cycles) and the benchmark checks pass; Bedrock 531 tests (3257 assertions) at
`-O0` and `-O2`, also built for `-O2 -march=x86-64-v3`; C4Testing 1100. See [../tools/05-test-suites.md](../tools/05-test-suites.md).

---

## 1. Feature matrix

### Program structure and preprocessor
| Feature | Status | Notes | Page |
|---|---|---|---|
| Whole-program compilation, parse-once includes | **Implemented** | Parallel parse, include closure, cycle recovery | [program model](../language/01-program-model.md) |
| Include-closure visibility for bodies | **Implemented** | Declarations (fields, signatures) resolve program-wide | [program model](../language/01-program-model.md) |
| File-scope order independence | **Implemented** | A file-scope `const` folds in every position before its declaration (B24) | [program model](../language/01-program-model.md) |
| Namespaces | **Implemented** | | [program model](../language/01-program-model.md) |
| `#define` / function-like / variadic macros, `#undef` | **Implemented** | | [preprocessor](../language/02-preprocessor.md) |
| `#if` family, `#` / `##`, `\` continuation, `#error` / `#warning` / `#pragma`, calls across lines | **Implemented** | | [preprocessor](../language/02-preprocessor.md) |
| Literals: integer suffixes and C typing, octal, `char8` / `char16` / `char32` characters and strings, the full escape set, adjacent strings | **Implemented** | | [lexical](../language/03-lexical-structure.md) |
| Predefined macros, `-D` | **Implemented** | `TEST`, `PLATFORM_WINDOWS`, `ARCH_X64`, `C4_VERSION`, `__FILE__`, `__LINE__` | [preprocessor](../language/02-preprocessor.md#26-predefined-macros) |

### Types and objects
| Feature | Status | Notes | Page |
|---|---|---|---|
| Primitives incl. `snorm` / `unorm`, `char8` / `char16` / `char32`; built-in aliases `int`, `uint`, `char` | **Implemented** | | [types](../language/04-types.md), [lexical](../language/03-lexical-structure.md) |
| Reference fields: bound where the object starts, read and written through, never owning (B43) | **Implemented** | | [types](../language/04-types.md#44-references) |
| Type-side arrays, pointers, references, const layers (`int32 const* p`, read-only locals, parameters, fields; `&x` of const is `const T*`; one pointer conversion rule) | **Implemented** | | [types](../language/04-types.md#42-declarators-modifiers-belong-to-the-type) |
| `funcptr<R(A)>`, `func<R(A)>` | **Implemented** | | [types](../language/04-types.md#46-function-pointer-types-funcptr-and-func) |
| `alias` (file, class, function scope; alias templates) | **Implemented** | | [types](../language/04-types.md) |
| `enum`, `enum class` with underlying type | **Implemented** | | [types](../language/04-types.md) |
| `class` / `struct` / `interface`, layout, nested and local types | **Implemented** | | [objects](../language/05-objects.md) |
| Static data members and methods | **Implemented** | | [objects](../language/05-objects.md) |
| Unions, anonymous unions | **Implemented** | | [objects](../language/05-objects.md) |
| Visibility | **Implemented** | Labels require inline modifiers; private / protected checked for fields, methods, operators and creates | [objects](../language/05-objects.md) |
| Single inheritance, virtual / override / pure virtual | **Implemented** | MSVC layout, vtables and RTTI; slots keyed by signature | [objects](../language/05-objects.md) |
| Multiple interfaces | **Implemented** | One class base (first) + interfaces; per-sub-object vtables, thunks, adjusted conversions; `p->Base::F()` on an explicit receiver (B35) | [objects](../language/05-objects.md) |
| Const methods, const overloads | **Implemented** | | [objects](../language/05-objects.md#const-methods) |
| `abstract`, `final` | **Implemented** | No object of an abstract type; no deriving from a final class or overriding a final method | [objects](../language/05-objects.md#abstract-and-final) |
| `volatile`, `thread_local` | **Implemented** | Volatile loads and stores; per-thread storage of any type; each thread starts its own copies (globals when it attaches, locals on its first pass) and destroys them when it ends; managed pointers in it are roots on every thread | [types](../language/04-types.md#413-storage-modifiers-volatile-and-thread_local) |
| Extension methods (also templated: `template<T> ... extends Box<T>`) | **Implemented** | | [objects](../language/05-objects.md) |
| `packed` / `unpacked` SIMD lane views and swizzles | **Implemented** | Composition/promotion optimization passes planned | [vectors](../language/11-vectors-and-swizzling.md) |
| Strict aliasing | **Implemented** | C++'s rule: `!tbaa` tags on loads and stores (bytes, unions and mixed objects alias everything); `-fno-strict-aliasing` turns it off (D-64) | [types](../language/04-types.md#414-strict-aliasing) |
| `packed T[N]` vector values, lane-wise operators, masks | **Implemented** | A scalar broadcasts into one; lane-wise casts and `reinterpret_cast` between vectors (D-62) | [vectors](../language/11-vectors-and-swizzling.md#7-packed-arrays-as-values) |
| The SIMD target: `-march`, `-m<feature>`, `SIMD_*` macros, 256- and 512-bit vectors | **Implemented** | One target per build, also for foreign sources and a host's scripts; alignment honored on the stack, heap and in containers (D-62) | [vectors](../language/11-vectors-and-swizzling.md#8-the-simd-target) |
| SIMD intrinsics (`simd_*`) | **Implemented** | Lane math, fused multiply-add, masks, shuffles, reductions in a fixed order, loads, stores, gathers (D-62) | [builtins](../language/14-builtins.md#11-simd-intrinsics) |

### Lifecycle and operators
| Feature | Status | Notes | Page |
|---|---|---|---|
| `[operator("create")]` / `[operator("delete")]`: C++ construction and destruction order, RAII for locals, parameters and temporaries, exception-safe construction | **Implemented** | | [lifecycle](../language/06-lifecycle-and-operators.md) |
| Field initializers; globals, static members and static locals started (declaration order / first pass, thread-safe once) and destroyed at exit | **Implemented** | | [lifecycle](../language/06-lifecycle-and-operators.md#612-static-storage) |
| Operator overloading (full table), compound fallback, index / index_assign | **Implemented** | Token spellings bind only their binary meaning (`"-"` is always `subtract`); only the first name of a multi-name `[operator]` is dispatched | [lifecycle](../language/06-lifecycle-and-operators.md) |
| Custom operators, `x.operator_name()` | **Implemented** | | [lifecycle](../language/06-lifecycle-and-operators.md) |
| `[operator("cast")]` | **Implemented** | | [lifecycle](../language/06-lifecycle-and-operators.md) |
| Iteration protocol (`begin` / `end` / ...) | **Implemented** | Loop variable is always a copy | [control flow](../language/09-statements-and-control-flow.md) |
| Copy (`[operator("copy")]` or member by member), assignment (D-06), moves of temporaries, returned locals and locals at their last use, `take(x)`, placement `new` / `destroy` / `relocate` | **Implemented** | | [lifecycle](../language/06-lifecycle-and-operators.md#64-copy-move-and-assignment) |

### Functions and templates
| Feature | Status | Notes | Page |
|---|---|---|---|
| Overloading, out-of-line methods | **Implemented** | | [functions](../language/07-functions-and-lambdas.md) |
| Default arguments | **Implemented** | Any expression, bound at the declaration, evaluated per call | [functions](../language/07-functions-and-lambdas.md#74-default-arguments) |
| `sizeof` / `alignof` / `offsetof` | **Implemented** | | [expressions](../language/10-expressions.md#41-sizeof-alignof-offsetof) |
| C variadics + `va_list` | **Implemented** | `va_start` / `va_arg(ap, T)` / `va_copy` / `va_end`; C variadics bind with their `...` | [functions](../language/07-functions-and-lambdas.md#77-c-variadic-functions) |
| Lambdas: C++ captures, closures, `mutable`, `func<R(A)>`, the call operator | **Implemented** | | [functions](../language/07-functions-and-lambdas.md#76-lambdas-and-closures) |
| Multiple return values: return lists, value lists, destructuring | **Implemented** | | [functions](../language/07-functions-and-lambdas.md#79-multiple-return-values) |
| Class / function / alias templates, value params, default template args, members compiled on use | **Implemented** | | [templates](../language/08-templates.md) |
| Template argument deduction, function templates in overload sets, deduction from a target funcptr/func type | **Implemented** | | [templates](../language/08-templates.md#5-argument-deduction) |
| Variadic templates: packs, expansions, `sizeof...`, folds | **Implemented** | | [templates](../language/08-templates.md#6-variadic-templates) |
| Specialization: full and partial, partial ordering | **Implemented** | | [templates](../language/08-templates.md#7-specialization) |
| Member function templates | **Implemented** | | [templates](../language/08-templates.md#25-member-function-templates) |

### Statements and expressions
| Feature | Status | Notes | Page |
|---|---|---|---|
| if / while / do / for / range-for / switch / `fallthrough` / break / continue | **Implemented** | | [control flow](../language/09-statements-and-control-flow.md) |
| `goto` / labels | **Implemented** | Jump-scope rules enforced | [control flow](../language/09-statements-and-control-flow.md) |
| `asm` statements and file-scope `asm` | **Implemented** | x86-64; an assembler error is reported at the asm block's line | [inline assembly](../language/16-inline-assembly.md) |
| `defer` | **Implemented** | Every exit edge, including exceptions; `finally` is a defer | [control flow](../language/09-statements-and-control-flow.md) |
| `try` / `catch` / `finally` / `throw` / rethrow | **Implemented** | Catch by value, by reference, by base, `catch (...)`, across C++ interop; exception objects are copied and destroyed by their lifecycle functions; `noexcept` stops the program | [control flow](../language/09-statements-and-control-flow.md) |
| Operator precedence | **Implemented** | C++'s table; `is`/`as` relational, `**` above prefix operators | [expressions](../language/10-expressions.md) |
| `is` / `as` | **Implemented** | Checked through RTTI for pointers to polymorphic objects; `as T&` throws BadCast (B34) | [expressions](../language/10-expressions.md) |
| `static_cast` / `dynamic_cast` / ... | **Implemented** | Each allows what C++'s does (D-12, B34); `dynamic_cast<T*>` is checked; `reinterpret_cast` also keeps the bits of a vector (D-62) or a number (D-64) as another of the same size | [expressions](../language/10-expressions.md#4-unary-expressions) |
| `**` power | **Implemented** | Integers (repeated squaring) and floats (`pow`); folds in constants; `[operator("power")]` on objects | [expressions](../language/10-expressions.md) |
| `new` / `delete`, aggregate / nested / designated / range initializers | **Implemented** | | [expressions](../language/10-expressions.md) |
| `new T[n]` / `new T[n]{...}` / `delete[]` | **Implemented** | Run-time count, `BadArrayLength`, elements destroyed exactly once (B42) | [expressions](../language/10-expressions.md#81-arrays-new-tn) |

### Interop, runtime, library
| Feature | Status | Notes | Page |
|---|---|---|---|
| `extern "c"` / `"c++"` include + source, CBind, `[link]`, COM | **Implemented** | Exceptions, destructors and C4 interfaces cross the boundary; overloads, const methods, static data members, access and signedness bind as in C++; CBind ships in every install and every toolchain | [interop](../language/13-interop.md) |
| Garbage collector | **Implemented** | Conservative, non-moving, over every registered thread, per-thread allocation caches (D-48); heaps past 16 MiB live marked concurrently with stops under ~1 ms (D-79), smaller ones stop-the-world; pools for custom allocators (D-78); Windows only; `-gc-stats` prints the statistics after each collection | [memory and GC](../runtime/01-memory-and-gc.md) |
| Reflection query API, qualified identity, annotations, unload listeners | **Implemented** | Thread-safe registry; a source must not be unloaded while other threads read its handles | [reflection](../runtime/02-reflection.md) |
| Reflection invoke / field get-set | **Implemented** | | [reflection](../runtime/02-reflection.md#32-calling-functions-and-methods) |
| Unit testing | **Implemented** | Failures report the test's line, not the assertion's; an exception in a test ends the run | [testing](../runtime/03-testing.md) |
| Bedrock containers (Array, List, Queue, Stack, HashMap), strings, format, files, paths, directories, DOM, JSON / YAML read and write, binary | **Implemented** | One-level namespaces (D-58); one YAML document, no anchors or tags | [Bedrock](../library/bedrock.md) |
| Math library (`include "math.ch"`, D-46) | **Implemented** | | [Bedrock](../library/bedrock.md#9-math-math-include-mathch) |
| Runtime threads, waiting on an address, atomics (D-60); Bedrock `Threading`: `Atomic<T>`, `Thread`, `Mutex`, `ConditionVariable`, `WaitGroup`, `Promise<T>` / `Future<T>`, `JobSystem` (D-61) | **Implemented** | | [memory and GC](../runtime/01-memory-and-gc.md#3-what-c4-code-can-reach) |
| Bedrock `Platform` (D-63): virtual memory, processors and memory, the executable's path, environment variables, libraries, wall-clock and calendar time | **Implemented** | Windows x64 | [Bedrock](../library/bedrock.md#12-platform-platform) |
| Bedrock `Net` (D-65, D-70): addresses, TCP, UDP, resolution, `Poll`, `EventLoop` with timers and posted work | **Implemented** | Blocking calls with timeouts; the loop is readiness-based (`WSAPoll`) | [Bedrock](../library/bedrock.md#13-networking-net) |
| Bedrock `Http` (D-66, D-67): HTTP/1.1 server and client, WebSockets; `Core::Sha1`, `Core::Base64*` | **Implemented** | Plain http and ws; no TLS | [Bedrock](../library/bedrock.md#14-http-http) |
| Bedrock `Process` (D-68): child processes with pipes, job objects, terminals | **Implemented** | Windows 10 1809+ for terminals | [Bedrock](../library/bedrock.md#15-processes-process) |
| Bedrock `FS` information, copies, canonical paths, `Watcher` (D-69) | **Implemented** | No file locking or memory-mapped files yet | [Bedrock](../library/bedrock.md#5-files-paths-and-directories-fs) |
| Bedrock serialization through reflection (D-72): strings, arrays, maps, enums, objects to and from JSON and YAML; `nameof` | **Implemented** | New array and map elements start zero, not created | [Bedrock](../library/bedrock.md#6-serialization-serialization) |
| Bedrock `Core::Regex` (D-71): RE2 syntax, linear-time Pike VM | **Implemented** | No backreferences or lookaround; ASCII case folding | [Bedrock](../library/bedrock.md#regular-expressions-regexch) |
| Streaming reads (D-77): `FS::ChunkReader`, `Serialization::JsonStream` / `JsonFileStream`, `BinaryStream` | **Implemented** | | [Bedrock](../library/bedrock.md#6-serialization-serialization) |

### Tools and platform
| Feature | Status | Notes | Page |
|---|---|---|---|
| `C4.exe` | **Implemented** | Every flag works and `-h` lists them; a copied install builds anywhere (D-56) | [compiler CLI](../tools/01-compiler-cli.md) |
| Nuke | **Implemented** | At the repository root (`../Nuke`): TOML manifests, separate `nuke test` builds, versioned C4 toolchains; `nuke gates` runs the compiler's test suites | [Nuke](../tools/02-nuke.md) |
| C4LSP | **Implemented** | The server is the `C4LSPLib` library, unit tested in C4Testing | [editor](../tools/03-editor-lsp-dap.md) |
| Debug information (`-g`) | **Implemented** | CodeView and a `.pdb` on Windows; Nuke uses it for Debug and Development | [codegen](../compiler/04-codegen-and-abi.md#debug-information--g) |
| C4DAP | **Implemented** | Launch only (no attach); no conditional or log points, no `setVariable` | [editor](../tools/03-editor-lsp-dap.md#3-c4dap-debugger) |
| Platforms | **Partial** | Windows x64 only | [roadmap](roadmap.md) |

---

## 2. Known bugs

Every open bug, including design rules the compiler does not enforce yet, is tracked in
[known-bugs.md](known-bugs.md). That file is updated as bugs are found and fixed.

---

## 3. Code-health items

- None open. B26 split every file over the ~600-line rule and `ExprLowerer::ResolveCall` into named steps (see
  [../compiler/README.md](../compiler/README.md)), retired `C4Testing/disabled_tests/` (its meaningful cases are
  `assets/IsolatedTest` and `assets/NegativeTest` folders) and removed `assets/Probes/`.

## 4. Other documents known to be out of date

None known. The documents outside `docs/` (`AGENTS.md`, the skills in `skills/`, `assets/IsolatedTest/README.md`)
were checked against the code in B26.
