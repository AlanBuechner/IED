# Implementation plan

The plan that takes the compiler from where it is today (2026-10-18) to a compiler that implements
everything in these docs with no open bugs. It is written for the agents that implement it: every
design question is already answered here or in the owning doc page, every work package says which
files to touch, what to build, and how to prove it works.

Status of the plan itself: **active**. Update it in the same change as the work it describes.

---

## 1. How to use this plan

1. Pick the lowest-numbered work package (WP) in section 5 whose status is **Ready** and whose
   dependencies are **Done**. Do one WP per change. Do not start a WP whose design says
   **Architect** - those are being done (or were done) by the architect pass in section 4.
2. Read the WP's **Design** links before writing code. The normative text in `docs/language/`,
   `docs/runtime/` and `docs/compiler/` is the spec. If the spec and this plan disagree, the spec wins;
   fix the plan.
3. Follow `AGENTS.md` (repo root) for code standards: honest functions, one level of abstraction,
   files <= ~600 lines as 1:1 `.h/.cpp` pairs, minimal comments, no dead code, ASCII only.
4. Every WP lists **Tests**. Write them first where you can (a failing repro folder), then the fix.
   Every new diagnostic gets a NegativeTest. Every runtime behavior gets an e2e test (TestCode or an
   IsolatedTest folder) that runs at `-O0` and `-O2`.
5. Bugs: when a WP fixes a `BUG-NNN`, delete its entry from [known-bugs.md](known-bugs.md) in the same
   change and name the test after the bug (`Bug054VirtualSlotOrder`). A bug you find while working is
   added to known-bugs.md immediately (next free id), then fixed - pre-existing is never a reason to
   leave it.
6. When the WP is done: run every gate (section 2), update the docs it lists (status lines,
   "Current implementation" notes, [implementation-status.md](implementation-status.md)), set the WP to
   **Done** here, and move any follow-up into a new WP.

### WP status values

| Status | Meaning |
|---|---|
| **Done** | Merged, gates green, docs updated |
| **Architect** | Reserved for the architect pass (design-heavy or re-architecture); do not start |
| **Ready** | Fully specified; dependencies done or none; an implementer can start |
| **Blocked** | Waiting on the listed dependency |

---

## 2. Gates (every WP, no exceptions)

Run from the C4 root (`C:/Work/C4` in these examples; pass your own checkout to `-p`).

```bat
cmd /c "Build.bat -b -c Development -a x64 -p C:/Work/C4"
python Frost/Build.py -test -c Development -a x64 -p C:/Work/C4
powershell -File assets/TestCode/RunTests.ps1
powershell -File assets/NegativeTest/RunTests.ps1
powershell -File assets/IsolatedTest/RunTests.ps1
powershell -File assets/IsolatedTest/RunTests.ps1 -O2
bin\Development-windows-x64\C4\C4.exe Bedrock\Tests -o assets\build-output\bedrock\t.exe -test && assets\build-output\bedrock\t.exe
powershell -File assets/RuntimeTest/RunTests.ps1
powershell -File assets/ScriptTest/RunTests.ps1
powershell -File assets/ScriptTest/RunTests.ps1 -O2
powershell -File assets/ToolsTest/RunTests.ps1
powershell -File Benchmarks/gemm/run-benchmark.ps1 -Check
```

- Grep the build log for `: error|error C\d|fatal error` (Frost can report success on a failed
  compile) and check that each changed `.cpp`'s `.obj` is newer than the source.
- After renaming or deleting a `.cpp`, delete its orphan `.obj` under `bin-int/` (orphans are linked).
- When the runtime, ABI or interop changes, also build `assets/StressTest` and the CBind tests.
- Baseline on 2026-10-18 (start of this plan): TestCode 833 assertions / 0 failed (`-O0` and `-O2`),
  NegativeTest 43/43, IsolatedTest 130/130 (`-O0` and `-O2`), Bedrock 104 tests / 443 assertions,
  C4Testing 491 tests with 4 failures (fixed in the architect pass, see section 4).

---

## 3. Design decisions

Every open question in the docs is decided here. The decision text is normative and is also written
into the owning page (the **Page** column), which is where implementers read the details. Decisions
marked (owner) were made by the project owner in notes on the doc pages.

| Id | Topic | Decision | Page |
|---|---|---|---|
| D-01 | `int` / `uint` (owner) | `int` = `int32`, `uint` = `uint32` as built-in aliases. The LSP colors every built-in alias as a built-in type. | [types](../language/04-types.md#41-primitive-types) |
| D-02 | `char` family (owner) | The byte character type is `char8`; `char` is a built-in alias of `char8`. Literals: `'a'` is `char8`, `u'a'` is `char16`, `U'a'` is `char32`; `"..."` and `u8"..."` are `char8*`, `u"..."` is `char16*`, `U"..."` is `char32*`. Escapes `\a \b \f \v \e \uXXXX \UXXXXXXXX` are added; unknown escapes are errors. | [lexical](../language/03-lexical-structure.md) |
| D-03 | Destructor name (owner) | `[operator("delete")]` is the only destructor. `"destroy"` and `"finalizer"` are errors with a fix-it (`use [operator("delete")]`). Every destruction - scope exit, `delete p`, GC reclamation - runs the same `delete` sequence. CBind emits `"delete"`. | [lifecycle](../language/06-lifecycle-and-operators.md#63-destruction-delete) |
| D-04 | `loop` (owner) | Removed. `loop` is an ordinary identifier. | [lexical](../language/03-lexical-structure.md#32-keywords) |
| D-05 | Value semantics, copy and move (owner delegated) | No `T&&`, no move operator, no value categories visible to the user. Objects are copied by `[operator("copy")]` (or memberwise), and the compiler **relocates** (bitwise move, source not destroyed) instead of copying whenever the source is a temporary or a returned local, or a local at its last use whose address never escaped (all guaranteed; the last use since A10). `take(x)` relocates out of any lvalue and leaves it zeroed. Zero is a valid empty state for every type, and `delete` must accept it. Objects are trivially relocatable: a type must not store its own address. | [lifecycle](../language/06-lifecycle-and-operators.md#64-copy-move-and-assignment) |
| D-06 | Assignment | `a = b` on objects calls `[operator("=")]` / `"assign"` when declared; otherwise trivially copyable types copy bytes, and other types run `delete` on `a`, zero it and copy (or relocate) `b` into it, skipping self-assignment. | [lifecycle](../language/06-lifecycle-and-operators.md#64-copy-move-and-assignment) |
| D-07 | Construction and destruction order | C++ order. Construction: zero, base `create`, member default `create`s in declaration order, own `create` body. Destruction: own `delete` body, member `delete`s in reverse order, base `delete`. A `create` that wants a base `create` with arguments calls it as its **first statement** (`Base::Init(x);`); otherwise the base's 0-argument `create` runs implicitly (error if the base has creates but no 0-argument one). | [lifecycle](../language/06-lifecycle-and-operators.md#62-construction-create) |
| D-08 | Multiple interfaces | One class base (written first) plus any number of interfaces. Interfaces have no instance fields, are always polymorphic, and their methods are public and virtual. Layout, vtables and RTTI follow the MSVC x64 ABI so C4 classes can implement C++ abstract interfaces and COM interfaces. | [objects](../language/05-objects.md#58-inheritance-and-interfaces), [codegen](../compiler/04-codegen-and-abi.md) |
| D-09 | Virtual slots | A slot is identified by name plus parameter types. A derived type inherits its primary base's slots in the base's order, overrides replace them in place, new virtuals are appended in declaration order. Interface sub-objects get their own vtables whose entries are this-adjusting thunks. | [objects](../language/05-objects.md#59-virtual-methods) |
| D-10 | Runtime type identity | Every polymorphic type gets MSVC-format RTTI (TypeDescriptor, CompleteObjectLocator at vtable[-1], class hierarchy and base class arrays). `is`, `as`, `dynamic_cast` and `catch` all read these tables. | [expressions](../language/10-expressions.md#7-type-tests-is-and-as) |
| D-11 | `is` / `as` | `x is T` is true when the dynamic type of `x` is `T` or derives from / implements `T` (compile-time constant for non-polymorphic operands). `x as T` is defined for pointer and reference targets: a checked cast that yields `null` (pointer) or throws `BadCast` (reference) on mismatch. `as` with a numeric target is an error with the fix-it `use (T)x`. `dynamic_cast<T*>(p)` is `p as T*`. | [expressions](../language/10-expressions.md#7-type-tests-is-and-as) |
| D-12 | C++-style casts | `static_cast`: numeric conversions and conversions between related object pointers (no runtime check). `reinterpret_cast`: pointer <-> pointer / integer bit reinterpretation, and (D-62, D-64) the bits of a packed vector or a number as another of the same size. `const_cast`: adds or removes const only. `dynamic_cast`: checked (D-11). Each rejects what it does not allow. | [expressions](../language/10-expressions.md#4-unary-expressions) |
| D-13 | Exceptions | Native MSVC EH. `catch (T e)`, `catch (const T& e)`, `catch (T* e)` match the exact type or any base (catch-by-base), `catch (...)` catches everything. Thrown objects are copied or relocated into the exception object and destroyed with `delete` after the handler. Type descriptors carry MSVC decorated names, so C++ and C4 exceptions cross the interop boundary in both directions. `finally` is a defer that covers the try and its handlers. `noexcept` functions are `nounwind`; an exception escaping one terminates. | [control flow](../language/09-statements-and-control-flow.md#8-exceptions) |
| D-14 | Scope exit | `defer` and value-local destructors run on every exit edge (fall-through, `return`, `break`, `continue`, `goto`, `fallthrough`, exception) via one cleanup stack in codegen. A `defer` body may not `return`, `break`/`continue` out of itself, `goto` out of itself, or contain labels. | [control flow](../language/09-statements-and-control-flow.md#7-defer) |
| D-15 | `goto` rules | A `goto` may jump to a label in its own block or any enclosing block. Jumping into a nested block, or forward past a declaration or a `defer` in the target block, is an error (`jump to label 'x' crosses the declaration of 'y'`). Backward jumps destroy what they leave. | [control flow](../language/09-statements-and-control-flow.md#5-goto-and-labels) |
| D-16 | Template member instantiation | C++ rule: a member function of a class-template instance is lowered and type-checked only when used (called, address taken, or a virtual of an instance whose vtable is emitted). Non-template code is always fully checked. | [templates](../language/08-templates.md#4-instantiation-semantics) |
| D-17 | Template argument deduction | C++ deduction from call arguments (with array-to-pointer and function-to-pointer decay, `const T&` binding, `Box<T>` patterns, `T[N]` deducing `N`, `funcptr<R(A)>` patterns). Explicit arguments may be given for a prefix and the rest deduced. A non-template function beats a template on an otherwise equal match. Conflicting deductions are an error naming both types. | [templates](../language/08-templates.md#5-argument-deduction) |
| D-18 | Default template arguments | `template<T = int32, uint32 N = 4>`; defaults are used for trailing omitted arguments and when deduction cannot determine a parameter. | [templates](../language/08-templates.md#1-template-parameter-lists) |
| D-19 | Variadic templates | `template<...Ts>` type packs and `template<...int32 Ns>` value packs; `Ts... args` parameter packs; expansions `f(args...)`, `f(g(args)...)`, `Box<Ts...>`; `sizeof...(Ts)`; fold expressions `(args + ...)`, `(... + args)`, `(args + ... + init)`. | [templates](../language/08-templates.md#6-variadic-templates) |
| D-20 | Specialization | Explicit full specialization of class and function templates (`template<> struct Box<bool> { ... }`, `template<> void F<int32>(int32 v)`) and partial specialization of class templates (`template<T> struct Box<T*> { ... }`), with C++ matching (most specialized wins, ambiguity is an error). No concepts/constraints. | [templates](../language/08-templates.md#7-specialization) |
| D-21 | Templated extension methods | `template<T> R F(...) extends Box<T>` is found by deducing `T` from the receiver type; non-template extensions on an instance (`extends Box<int32>`) and on primitives keep working. | [objects](../language/05-objects.md#510-extension-methods-extends) |
| D-22 | Lambdas and closures (owner: C++-style captures) | C++ capture syntax: `[]`, `[=]`, `[&]`, `[x, &y]`, `[this]`, `[*this]`, init captures `[n = expr]`, `[&r = expr]`, optional `mutable`. Each lambda has a unique closure type with a `[operator("call")]`. A capture-less lambda converts to `funcptr<R(A)>`. Any lambda (any object with a matching call operator) converts to `func<R(A)>`, a 16-byte `{code, env}` value: the env is a GC-allocated copy of the closure, except that a closure whose only capture is one pointer (`[this]`, `[&x]`) needs no allocation - the pointer is the env (revised during A6: a frame-slot env for by-reference closures dangles when the func outlives the frame, even when the referents are alive). Copying a func shares its env. Templates take the closure type directly for zero-cost calls. | [functions](../language/07-functions-and-lambdas.md#76-lambdas-and-closures) |
| D-23 | Call operator | `[operator("call")]` (token `"()"`): `x(args)` on an object calls it. | [lifecycle](../language/06-lifecycle-and-operators.md#65-the-operator-table) |
| D-24 | Operator arity | A token with a unary and a binary meaning (`- * & +`) binds by the method's parameter count. Every name in a multi-name `[operator(...)]` is registered. | [lifecycle](../language/06-lifecycle-and-operators.md#66-arity-and-resolution-rules) |
| D-25 | Operator receiver | `a op b` looks for the operator on `a`'s type only (members, bases, extensions). The compiler never swaps operands. `2.0f * v` needs an extension method on `float`. | [lifecycle](../language/06-lifecycle-and-operators.md#66-arity-and-resolution-rules) |
| D-26 | More operator names | `<<=` `shift_left_assign`, `>>=` `shift_right_assign`, `**` `power` (built in for numerics). | [lifecycle](../language/06-lifecycle-and-operators.md#65-the-operator-table) |
| D-27 | Precedence | C++ table. `is` / `as` sit with the relational operators (C#). `**` binds tighter than a unary operator on its left (`-x ** 2` is `-(x ** 2)`) and is right associative (Python rule). | [expressions](../language/10-expressions.md#1-precedence-normative) |
| D-28 | Visibility sections | Kept as `AGENTS.md` describes: a `public:` / `private:` / `protected:` label requires every member under it to carry the matching inline modifier (error with fix-it otherwise). Full C++ access checks for fields and methods; `protected` is visible to derived types; extension methods see only public members. | [objects](../language/05-objects.md#57-visibility) |
| D-29 | Modifiers | `abstract` (class cannot be instantiated), `final` (class cannot be derived, method cannot be overridden), `noexcept` (D-13), `volatile` (volatile loads/stores), `thread_local` (per-thread storage; managed pointers in it are scanned on every thread, D-48), trailing `const` on methods (D-31). `concurrent` is removed (error). | [lexical](../language/03-lexical-structure.md#33-contextual-identifiers) |
| D-30 | Const layers | `const` before the base type makes the base const; `const` written before a `*` makes that pointer layer const (`int32 const* p` is a const pointer to a mutable int32). Constness belongs to locations and pointees: the outermost layer's const is the variable's/field's/parameter's constness. `T* const` (const after the last layer) is an error with the fix-it `write T const*`. Top-level const in template arguments, return types and cast targets is ignored. | [types](../language/04-types.md#42-declarators-modifiers-belong-to-the-type) |
| D-31 | Const methods | Inside a `const` method `this` is read-only; a const object or a `const T&` / `const T*` can only call const methods. | [objects](../language/05-objects.md#59-virtual-methods) |
| D-32 | Implicit conversions | Unchanged table, plus: `const T&` binds to any expression convertible to `T` through a materialized temporary (C++ rule); `enum class` needs an explicit cast from its underlying integer (plain `enum` still converts implicitly); numeric narrowing stays implicit but warns once warnings exist (WP B22). | [types](../language/04-types.md#49-implicit-conversions) |
| D-33 | `T[]` | Not a type: error with the fix-it `use T* or Vector<T>`. | [types](../language/04-types.md#45-arrays) |
| D-34 | Multiple return values | `[float, bool] F() { return [1.0f, true]; }`, `[float a, bool ok] = F();`, `[a, float b] = F();`, `[a, _] = F();` (discard), `[a, b] = [b, a];` (all right-hand values are evaluated first). The return type is an anonymous struct laid out like a C struct and returned by the C ABI rules; return lists do not participate in overloading. | [functions](../language/07-functions-and-lambdas.md#79-multiple-return-values) |
| D-35 | Variadic arguments (owner: C++ style) | `va_list ap; va_start(ap); T x = va_arg(ap, T); va_copy(dst, src); va_end(ap);` - `va_arg` is an intrinsic whose second argument is a type. The `va_arg_int` family is removed. | [functions](../language/07-functions-and-lambdas.md#77-c-variadic-functions) |
| D-36 | Packed arrays as values (owner) | `packed T[N]` is a first-class value type (the SIMD vector `<N x T>`) usable for locals, parameters, returns and fields; it supports indexing and built-in elementwise arithmetic with scalar broadcast, and assigns to/from a class's packed array alias. N in 2..16 and `N * sizeof(T) <= 64`. | [vectors](../language/11-vectors-and-swizzling.md#7-packed-arrays-as-values) |
| D-37 | Swizzle rules | `&v.x` and `&v.xy` are errors; a write target with repeated lanes (`v.xx = ...`) is an error; lane names are one character. | [vectors](../language/11-vectors-and-swizzling.md#3-rules) |
| D-38 | Normalized integers | Conversion float -> `snormN`: clamp to [-1, 1], scale by `2^(N-1)-1`, round to nearest; `unormN`: clamp to [0, 1], scale by `2^N-1`. Conversion back divides (snorm clamps -max-1 to -1). Arithmetic on normalized values is done in `float` (`double` for 32/64-bit) and the result is that float type. | [types](../language/04-types.md#41-primitive-types) |
| D-39 | Switch fallthrough | Falling off the end of a non-empty case into the next one is an error (fix-it `add 'fallthrough;' or 'break;'`). Stacked empty labels (`case 1: case 2:`) are fine. | [control flow](../language/09-statements-and-control-flow.md#4-switch) |
| D-40 | Comma | No comma operator. The `for` step clause accepts a comma-separated list of expressions. | [expressions](../language/10-expressions.md#10-not-supported) |
| D-41 | New intrinsics | `alignof(T)`, `offsetof(T, field)`, `sizeof(expr)` for any expression (unevaluated), `take(x)` (D-05), `destroy(p)`, `relocate(dst, src)`, placement `new (p) T(args)`. No `typeof`, `decltype`, `auto`, `?.`, `??`. | [builtins](../language/14-builtins.md#1-compiler-intrinsics) |
| D-42 | Annotations | Arguments may be any constant expression, folded where the declaration is written; the compiler's own annotations take string literals. Unknown annotations are kept and exposed through reflection (name + folded arguments). A near miss of a compiler annotation (edit distance <= 2, e.g. `[operater]`) is a warning. | [annotations](../language/12-annotations.md) |
| D-43 | Prelude | Builtins move out of the compiler into `Bedrock/Prelude/prelude.ch`, an implicit include of every unit made of ordinary `[link]` declarations. Only true intrinsics stay in the compiler. `_GUID` is declared there. A type without `[uuid]` gets a deterministic name-based (v5) GUID so `__uuidof(T)` works for every type. | [builtins](../language/14-builtins.md) |
| D-44 | Predefined macros | `TEST` (=1 under `-test`; replaces `C4_TEST_MODE`), `PLATFORM_WINDOWS` / `PLATFORM_LINUX`, `ARCH_X64`, `C4_VERSION`, `__FILE__`, `__LINE__`, and `-D NAME[=VALUE]` on the command line. | [preprocessor](../language/02-preprocessor.md#26-predefined-macros) |
| D-45 | Assertions | Assertions link in every build (they are ordinary functions); test code is stripped from shipping builds with `#if TEST`. | [testing](../runtime/03-testing.md) |
| D-46 | Math library location | Bedrock ships the math library in `namespace Math` (one level, D-58): vectors with swizzles, matrices, quaternions, left-handed, 0-1 NDC, column-major like GLM, modeled on a game engine's math library and SIMD-optimized. It is opt-in through `include "math.ch"`, so a program that does not include it may still define its own `Math`. | [Bedrock](../library/bedrock.md) |
| D-47 | GC model | Conservative, non-moving mark-sweep with size-class spans, a page map for O(1) pointer lookup, an explicit mark stack, a large-object space and register capture before the stack scan (fixes the `-O2` bug without per-slot anchors). No write barrier until a concurrent or generational collector needs one. One consistent C API (`c4_gc_*`) and a Bedrock `GC` namespace over it. | [memory](../runtime/01-memory-and-gc.md) |
| D-48 | Threading and the GC | Suspend-and-scan (Boehm style): the collector suspends registered threads, captures their contexts and scans their stacks. Allocation uses per-thread caches per size class. No safepoint polls in generated code. | [memory](../runtime/01-memory-and-gc.md#5-intended-direction) |
| D-49 | Scripting model | Scripts are ordinary `.cf`/`.ch` files compiled at run time by the embedded C4Lib (`C4Script.dll`) and JIT-linked with ORCv2 against the host's symbols, sharing the host's GC, type system and reflection registry. A host is built with `--script-host`: a symbol table in the image and an interface beside it (`.c4i`: the host's sources after preprocessing with non-template bodies removed, its exported macros, symbol names, and bitcode of small functions for inlining), so scripts compile without the host's sources. A host template instance is shared; one the host never made is per script. Partial sandbox = crash isolation (SEH guard around calls through the scripting API; a faulted script refuses further calls until the host unloads it) plus an include allow-list; no memory quotas. Reload = run the script's exits, drop its roots, finalizers and reflection tag, remove its JITDylib, recompile, relink. | [scripting](../runtime/04-scripting.md) |
| D-50 | Reflection identity and invoke | Types register under their fully qualified, argument-aware name (`Geo::Box<int32>`). Every reflected function and method with code gets a uniform invoke thunk `void(void* object, void* result, void** arguments)` (a virtual method dispatches through `object`); fields get typed get/set through offsets. Registry storage is dynamic with a name index. Unknown annotations are reflected (D-42). | [reflection](../runtime/02-reflection.md) |
| D-51 | Header-private symbols, `using`, `namespace A::B {}` | No header-private symbols. No `using` (use `alias`). `namespace A::B { }` is accepted as shorthand for nested namespaces. | [program model](../language/01-program-model.md#19-namespaces) |
| D-52 | Implicit user conversions | `[operator("cast")]` is explicit only and `create` is never an implicit conversion (both are C++ `explicit`). | [lifecycle](../language/06-lifecycle-and-operators.md#68-conversion-operator-cast) |
| D-53 | `__init_list<T>` name | The intrinsic keeps its name; Bedrock exports `namespace Core { template<T> alias InitList = __init_list<T>; }`. | [types](../language/04-types.md#411-__init_listt) |
| D-54 | Range-for by reference | `for (T& v : range)` aliases each element (needs `index`/`dereference` returning `T&` or an array/pointer range); `for (const T& v : range)` aliases read-only. | [control flow](../language/09-statements-and-control-flow.md#3-range-based-for) |
| D-55 | Missing return | Control flow that can fall off the end of a non-`void` function is an error. | [functions](../language/07-functions-and-lambdas.md#71-function-declarations) |
| D-56 | Toolchain layout | `C4.exe` finds everything relative to itself: `Bedrock/`, `C4Runtime/` (runtime sources; their objects are cached in `%TEMP%/c4_runtime/<hash of the folder>/`), `toolchain/` (clang, lld-link, clang's headers) and `CBind/`, with `C4_HOME` as an override. It links with `clang -fuse-ld=lld`. The repo build produces this layout, and `MakeToolchain.py` assembles it into a Nuke toolchain. | [compiler CLI](../tools/01-compiler-cli.md#4-the-install) |
| D-57 | Braced arguments | A braced list argument binds an `__init_list<T>` parameter (cost 0), an object parameter (by value or `const T&`) that braces of that many entries can build - an aggregate with at least as many fields, or a create taking that many arguments (cost 3) - and an array parameter with at least as many elements (cost 3). The entries convert when the chosen overload binds; two equally good object parameters are ambiguous, naming both. As in C++, `Take({ 5, 6 })`. | [expressions](../language/10-expressions.md) |
| D-58 | Bedrock layout | Bedrock's names live in one-level namespaces: `Core` (containers, text, hashing, sorting, and anything that is not part of a larger system), `FS`, `Serialization`, `Reflection`, `GC`, `Math` (opt-in, D-46), `Threading` (D-61) and `Platform` (D-63). Counts, sizes and indices are `int64` and "not found" is `-1`; element access is unchecked; a zero container is empty and allocates nothing; the growable array is `Core::Array<T>`. A `HashMap` hashes and compares keys through a `KeyTraits<K>` type parameter whose primary uses the key's `hash` operator and `==`. | [Bedrock](../library/bedrock.md) |
| D-60 | Atomics | Compiler intrinsics on a pointer (`atomic_load`, `atomic_store`, `atomic_exchange`, `atomic_compare_exchange[_weak]`, `atomic_fetch_add/sub/and/or/xor`) and `atomic_fence`, each with a `MemoryOrder` (the prelude's enum: Relaxed, Acquire, Release, AcqRel, SeqCst) and C++'s rules for them (checked when the order is a constant; an order known only at run time selects among the valid ones, any other acting as SeqCst). They lower to LLVM atomics; Bedrock's `Atomic<T>` and the threading library build on them. | [builtins](../language/14-builtins.md#10-atomics) |
| D-61 | Threading library | Bedrock's `Threading` namespace is built on the atomics (D-60) and the runtime's threads and address waits, with no OS lock anywhere: `Atomic<T>`, `Thread` (joins when destroyed), `Mutex` and `ConditionVariable` (a word each, parked on with `wait_on_address`), `WaitGroup`, `Promise<T>` / `Future<T>`, and `JobSystem`. The job system is tuned for latency, not throughput: frame work (`High`, `Normal`) runs on frame workers and on any thread that helps while it waits, from bounded lock-free MPMC queues (one per frame worker and priority, which idle workers take from, plus shared ones); `Background` work (loading, long computations) runs only on background workers, so a time-critical job never waits behind a long one for a worker. Idle workers spin briefly, then park on an address. | [Bedrock](../library/bedrock.md#10-threading-threading) |
| D-59 | Projects | A build is a set of named projects (sources, include folders, defines, dependencies) compiled as one program. Each file belongs to one project, is preprocessed with its defines, resolves includes from its project and its dependencies, and sees no name of a project it does not depend on. The build tool describes the build in a TOML file (`--projects`); a plain command line is one project. | [projects](../language/15-projects.md) |
| D-62 | SIMD target, widths and intrinsics (roadmap 1a) | **One target per build.** `-march=<cpu>` (any LLVM x86-64 CPU: `x86-64`, the default, `x86-64-v2/v3/v4`, `haswell`, `znver4`, ..., or `native`, the building machine) and `-m<feature>` / `-mno-<feature>` (`sse3 ssse3 sse4.1 sse4.2 popcnt avx avx2 fma f16c bmi bmi2 lzcnt avx512f avx512vl avx512bw avx512dq avx512cd`) pick it; implied features follow LLVM (`-mavx2` turns on `avx`, `-mno-avx` turns off `avx2` and `fma`). Every function of the program is compiled for it, foreign C/C++ sources get the same flags, and a script host records it in its `.c4i` so its scripts compile and run for exactly that target: all code of one program agrees on how a vector is passed. Each enabled feature defines `SIMD_SSE3`, `SIMD_SSSE3`, `SIMD_SSE4_1`, `SIMD_SSE4_2`, `SIMD_AVX`, `SIMD_AVX2`, `SIMD_FMA`, `SIMD_AVX512F`, `SIMD_AVX512VL`, `SIMD_AVX512BW`, `SIMD_AVX512DQ` (= 1), and `SIMD_FLOAT_LANES` is 4, 8 or 16, the floats of the widest register. **Widths:** `packed T[N]` keeps 2..16 lanes and 64 bytes on every target; a vector wider than the target's registers is split by the compiler (a `packed float[8]` is two XMM registers without AVX). Its alignment (the largest power of two dividing its size: 32 for `packed float[8]`, 64 for `packed float[16]`) holds on the stack, in globals, on the managed heap and in Bedrock's containers. Win64 passes a vector argument by hidden reference and returns it in XMM0 / YMM0 / ZMM0 when the target has that register (split otherwise). **Values:** a scalar converts to a `packed T[N]` by broadcasting, as an operand already does; `(packed U[N])v` (also `static_cast`) converts lane by lane like the scalar cast, `packed bool[N]` lanes to 0 / 1; `reinterpret_cast<packed U[M]>(v)` keeps the bits of a vector of the same size. **Intrinsics** (`simd_*`; a program function of the same name wins) take `packed T[N]` values (a pure packed vector object reads as its lanes) and give `packed` values: lane math `simd_min` / `simd_max` (`a < b ? a : b`, the SSE rule: a NaN gives `b`), `simd_abs`, `simd_sqrt`, `simd_floor`, `simd_ceil`, `simd_round` (halves away from zero), `simd_trunc`, `simd_muladd` (`a * b + c`, fused when the target has FMA), `simd_fma` (always one rounding: needs `fma`); masks `simd_select(mask, a, b)`, `simd_any`, `simd_all`, `simd_bitmask` (a `uint32`, bit i = lane i); lanes `simd_shuffle(a, i...)` and `simd_shuffle(a, b, i...)` (constant indices; 0..N-1 name a's lanes, N..2N-1 b's), `simd_concat(a, b)`, `simd_splat(x, N)`; horizontal `simd_reduce_add` / `_mul` / `_min` / `_max` (and `_and` / `_or` / `_xor` on integers), floats in a fixed halving order (the upper half of the lanes onto the lower, padded with the identity), so a result is the same at every optimization level and target; memory `simd_load(p, N)`, `simd_store(p, v)` (element alignment), `simd_load_masked(p, mask)` / `simd_store_masked(p, v, mask)` (lanes off are zero and not touched), `simd_gather(p, indices)` / `simd_gather_masked(p, indices, mask)`. Errors name the shape at fault: vectors of different shapes, a lane count or index that is not a constant or out of range, a float-only intrinsic on integer lanes, `simd_fma` on a target without `fma`. | [vectors](../language/11-vectors-and-swizzling.md#8-the-simd-target), [builtins](../language/14-builtins.md#11-simd-intrinsics) |
| D-63 | Platform library | Bedrock's `Platform` namespace is a thin layer over a small C API in the runtime (`C4Runtime/PlatformRuntimeC.h`, one implementation file per area and OS, so a port adds files and changes no caller). Virtual memory is free functions on raw addresses (`Reserve`, `ReserveAligned`, `Allocate`, `Commit`, `Decommit`, `Release`, `Protect`, `Query`, `FlushInstructionCache`) that report failure with `false` / null; protection is an `enum class` of the six combinations x64 pages can have, so a write-only or write-execute page cannot be asked for. Memory it hands out is never scanned by the collector (`GC::RegisterRegion` it when it holds managed pointers). System and process information are plain structs filled in one call; the executable's path and environment variables are UTF-8 (`FS::Path`, `Core::String`). `Library` owns a loaded module as `FS::File` owns a file (closed on delete, not copyable, `Error()` says why it did not open). Time: the raw high-resolution counter, the wall clock as Unix nanoseconds, and calendar fields; the UTC conversion is plain C4, only the local offset comes from the OS. The monotonic clock stays the prelude's `clock_now_ns()`. | [Bedrock](../library/bedrock.md#12-platform-platform) |
| D-64 | Strict aliasing (roadmap 5.2) | **C++'s rule, on by default.** A value in memory is read and written only as its own type, where every integer of one size is one type (signed, unsigned, normalized, a character, an enum of that underlying type), an alias is its target, an array is its elements and a `packed T[N]` is its `T` lanes; as a byte type (`int8`, `uint8`, `char8`, `snorm8`, `unorm8`), which reads and writes any memory; through a union member (named or anonymous, or an element of an array member), which reads any member's bytes, so punning through a union works as in C; or as part of an object that contains it (a copy reads every field). Pointers, references, `funcptr` and `func` values are one type. Anything else, such as reading a `float` through an `int32*`, is undefined: the optimizer assumes it does not happen and may reorder or drop such accesses. A value's bits are read with `reinterpret_cast<uint32>(f)` (a number to another number of the same size, as D-62 does for vectors), a union, or a byte copy (`memcpy`, a `uint8*`). Unlike clang, which lets a vector type read anything, a `packed T[N]` reads only `T` memory: otherwise a loop storing vectors could not keep pointers it read in registers; `simd_load` from a `T*` or a `reinterpret_cast` of the vector covers what a C cast to `__m128i*` does. The compiler tells LLVM with type-based alias metadata (`!tbaa`, one node per class under a root `C4 TBAA`) on the program's loads and stores; `-fno-strict-aliasing` turns the rule off for a program (no metadata: any access may touch any memory, as MSVC compiles). Scripts follow the rule. Foreign C/C++ sources are separate objects compiled by clang, so interop is unchanged. | [types 4.14](../language/04-types.md#414-strict-aliasing), [expressions 4](../language/10-expressions.md#4-unary-expressions) |

Questions deliberately left to the project owner (not blocking any WP): whether the GC should become
optional per program (`-gc=off`); parameterized tests and benchmarks in the test framework.

---
## 4. Architect pass

The re-architecture and design-heavy packages. They are done by the architect, in this order,
because later work builds on their structure. Implementers must not start them; when one is
**Done**, its notes say what changed so dependent WPs can rely on it.

| WP | Title | Closes | Status |
|---|---|---|---|
| A0 | Baseline repairs found during the survey | BUG-057 (local functions parsed as direct-init), BUG-058 (corpus harness without Bedrock), BUG-059 (stale overload tie test) | **Done** |
| A1 | Cleanup-scope emission: one cleanup stack for defer, destructors, finally and EH | BUG-002, BUG-056, BUG-060, BUG-061, BUG-062, BUG-063; D-14, D-15 | **Done** |
| A2 | Object model: MSVC layout, vtables, interfaces, RTTI, casts, EH type tables | BUG-027, BUG-028, BUG-054, BUG-055, BUG-064, BUG-065, BUG-067..BUG-075; D-08..D-13 (cast rules and `as T&`: B34; `noexcept` codegen: A3) | **Done** |
| A3 | Value lifecycle: copy, assignment, relocation, member and base construction/destruction | BUG-004, BUG-009; D-05..D-07 | **Done** |
| A4 | Templates: on-demand members, deduction, overloaded function templates, defaults, templated extensions | BUG-014, BUG-015, BUG-066; D-16..D-18, D-21 | **Done** |
| A5 | GC runtime rewrite | BUG-031..BUG-035; D-47 (BUG-003 and BUG-053 closed early, see A1) | **Done** |
| A6 | Closures, `func<>`, call operator | D-22, D-23 | **Done** |
| A7 | Const layers and const methods | BUG-007, BUG-025, BUG-084; D-30, D-31 | **Done** |
| A8 | Variadic templates and specialization | BUG-115..BUG-118; D-17 (target types), D-19, D-20 | **Done** |
| A9 | Multiple return values | D-34 | **Done** |
| A10 | Last-use moves of locals (drop flags), D-05's last-use guarantee | D-05 | **Done** |

### A0 - Baseline repairs (Done)
- `VariableDefinitionNode`: inside a body, `T name(...)` followed by `{` is a local function (a new
  lookahead `ParenGroupIsFollowedByBody`); only `T name(args);` is direct initialization.
  Regression: `assets/IsolatedTest/Functions/LocalFunctionVsDirectInit`.
- `IsolatedCorpusModelTest` registers the repo's Bedrock umbrella like `C4.exe` does.
- `ModelParityTest`: the ambiguity case uses two same-category conversions (the cost table charges 2
  for int -> float); a new assertion checks same-category beats cross-category.

### A1 - Cleanup-scope emission (Done)
What changed, for the WPs that build on it:
- **Codegen:** `Backend/CodeGen/CleanupStack.{h,cpp}` holds cleanups, try scopes and catch handlers.
  `RegionEmitter` (rewritten) emits every exit edge through `ExitCleanups` / `BranchThroughCleanups` and
  creates cleanup pads lazily (`CleanupPadFor`). `IRContext` (rewritten) owns the stack, the unwind-target
  query (`UnwindTarget`), the funclet query (`CurrentFunclet`), the ONE call brick `Call(fty, callee, args,
  mayThrow)` and `EntryAlloca` / `Materialize` / `ReturnSlot` / `EnsureThrowSlot` (every alloca is an entry
  alloca). New code must create calls only through `IRContext::Call` and allocas only through `EntryAlloca`.
  Design: [04-codegen-and-abi.md, Scope exit and unwinding](../compiler/04-codegen-and-abi.md#scope-exit-and-unwinding).
- **HIR:** `StmtLowerer` was split into `StmtLowerer` (dispatch, blocks, return, if, switch, jumps),
  `DeclLowerer`, `LoopLowerer`, `ExceptionLowerer`, plus `IndexLValue` and `JumpScopes`. Every block is
  entered with a `RegionScope`. `finally F` lowers to `{ defer F; try ... }`; `TryNode` has no finally.
  A `for` statement's `initBlock` always exists and encloses the loop. Locals bind through
  `LoweringContext::BindLocal` and read through `LocalValue` - the slot lives on the scoped `SymbolInfo`
  (`localSlot`, `localIsReference`, `localRefersToConst`); the old name -> slot maps are gone.
- **Rules now enforced:** goto into a nested block, forward goto across a declaration or defer, goto to an
  undefined label, duplicate labels, and return / goto / labels / escaping break or continue inside a
  defer or finally body (10 NegativeTests).
- **Also fixed while there:** calls without funclet bundles (BUG-060), stack growth from mid-function
  allocas (BUG-061), fall-through into a label (BUG-062), shadowed locals reading the inner slot (BUG-063),
  a dangling `GetName().c_str()` in the EH descriptor builder, double conditions in `AsBool`, unnamed catch
  clauses, the IndexOp address. The write barrier is gone from codegen and the runtime (BUG-053); the
  collector now captures the register file before scanning the stack (BUG-003) - removing the barrier
  had been hiding that bug, because its call kept every pointer slot in memory.
- **Tests:** IsolatedTests `ControlFlow/DeferOnEveryExit`, `ControlFlow/ForInitLifetime`,
  `ControlFlow/GotoScopes`, `Exceptions/FinallyOnEveryExit`, `Exceptions/CallsInsideHandlers`,
  `Types/ShadowedLocalSlot`, `Calls/LoopTemporariesStack`, `MemoryGC/RegisterHeldPointers`,
  `Functions/LocalFunctionVsDirectInit`; NegativeTests `GotoIntoNestedBlock`, `GotoCrossesDeclaration`,
  `GotoCrossesDefer`, `GotoUndefinedLabel`, `DuplicateLabel`, `ReturnInDefer`, `ReturnInFinally`,
  `BreakOutOfDefer`, `ContinueOutOfDefer`, `GotoInDefer`.

### A2 - Object model (Done)
What changed, for the WPs that build on it:
- **Model:** `ObjectType` lays out MSVC-style (`GetBaseOffset(i)`, `GetPrimaryBase`, `HasOwnVptr`,
  `IsPolymorphic`, `IsInterface`; the old single-inheritance helpers and `VirtualMethodSlots` are gone)
  and carries its identity (`ObjectKind` from the declaration, `GetEnclosingNames`, `GetTemplateArguments`;
  set by `Model/Builder/ObjectIdentity`). Two bricks own the hierarchy facts:
  `Model/Types/ClassHierarchy` (`LocateBase`, `DerivesFrom`, `PointerDelta`, `ForEachSubobject`,
  `Subobjects`, `FindField`, `CheckBaseList`, `CheckInterfaceMembers`) and `Model/Types/VirtualTables`
  (`PrimarySlots`, `VptrLocations`, `FinalOverrider`, `DispatchFor`, `SameSignature`). Every hierarchy
  walk (member, operator and cast lookup, overload candidates, the inherited-clash check) visits every
  base, not only the first. New code must use these bricks instead of walking `GetBaseTypes()`.
- **Base list rules:** one class base, written first; interfaces only derive from interfaces and have
  no instance fields; no base twice; no base that another listed base already has.
- **HIR:** `Backend/HIR/MethodReceiver` (`BindReceiver`, `CallDispatch::Direct` for `Base::F()`,
  `SubobjectLValue`); `LValue::Kind::Base`; `DynamicCastNode` for `as` / `dynamic_cast` / `is`;
  `LoweringContext::AddressAs` (a reference binding of a derived object converts to the base);
  `Backend/HIR/IndirectCall` (funcptr calls: arity and argument conversions); catch clauses carry
  `byReference`, `constQualified`, `catchAll`; the parser accepts `catch (...)`.
- **Codegen:** `Abi::Signature` / `ModuleContext::SignatureOf` is the one source of hidden-parameter
  order (member: `this`, sret, all objects by sret; extension: sret, receiver; free: sret) - every
  declaration, prologue, call and thunk uses it. `CallEmitter` and `ObjectEmitter` were split out of
  `ExprEmitter`; constructors are called through `CallEmitter::EmitConstructor`. New bricks:
  `VtableEmitter` (vtables per vptr location, thunks, `StoreVptrs`, `ZeroImage`), `RttiEmitter`,
  `EhTables`, `MsvcNames`, `PeTables`. `ModuleContext::VtableFor` / `EnsureEhType` are gone. Design:
  [04-codegen-and-abi.md](../compiler/04-codegen-and-abi.md#objects-vtables-and-construction).
- **Runtime:** `C4Runtime/RttiRuntimeC.c` (`c4_dynamic_cast`, `c4_instance_of`, `c4_purecall`), linked
  into every executable (`EnsureRttiRuntimeObject`).
- **Tools:** CBind binds functions and methods with function-pointer parameters (the shim prints named
  declarators). Frost gained `toolPaths`: the root build now builds CBind and C4DAP (`Build.py`), and
  `Build.py -test` exits 0 only when C4Testing, CoreTesting and ReflectionTesting all pass.
- **Also fixed while there:** BUG-064 (inherited fields addressed inside the base), BUG-065 (member
  calling convention), BUG-067 (vptrs of globals, statics, fields, array elements), BUG-068 (funcptr
  calls with sret, argument conversion), BUG-069 (virtual call receiver evaluated twice), BUG-070
  (constructor arguments ignored the ABI), BUG-071 (derived object to base reference parameter), BUG-072
  (CBind skipped function-pointer parameters), BUG-073 (CBind / C4DAP never rebuilt), BUG-074
  (ReflectionTesting fixtures in the `Testing` group crashed at exit), BUG-075 (Frost `mklink` paths),
  `Base::F()` through a namespace path still dispatching, slicing copies keeping a derived vtable, and
  the structural signature compare in the inherited-clash check.
- **Tests:** IsolatedTests `OOP/VirtualSlots`, `OOP/MultipleInterfaces`, `OOP/CheckedCasts`,
  `OOP/PolymorphicStorage`, `OOP/SlicingKeepsOwnVtable`, `OOP/IsOnNonPolymorphicPointer`,
  `Calls/VirtualReceiverOnce`, `Calls/FunPtrCallAbi`, `Calls/ConstructorArgumentAbi`,
  `Exceptions/CatchByBase`, `Exceptions/CatchAll`, `Exceptions/CatchByReference`,
  `Interop/CppExceptionsAndInterfaces`; NegativeTests `TwoClassBases`, `ClassBaseAfterInterface`,
  `InterfaceDerivesFromClass`, `InterfaceWithField`, `DuplicateBase`, `BaseAlreadyInherited`,
  `NumericSafeCast`, `DowncastWithoutVirtuals`, `AmbiguousInheritedMethod`, `FunPtrArgumentCount`,
  `FunPtrArgumentType`; C4Testing `Semantic.ObjectModel` (9 tests) and the rewritten `SafeCast*` HIR tests.
  (`NegativeTest/UnloweredExpression` asserted that `is` is unsupported; it became the positive test.)
- **Left for later, with owners:** exception object copy / destroy thunks and `noexcept` (A3);
  template instance names with MSVC back-references (A4); D-12 per-cast rules and `as T&` (B34);
  `p->Base::F()` on an explicit receiver (B35); abstract-type checks (B7).

### A3 - Value lifecycle (Done)
What changed, for the WPs that build on it:
- **Language:** [lifecycle 6.2-6.4](../language/06-lifecycle-and-operators.md#62-construction-create) is
  normative: C++ construction and destruction order (a base create with arguments is the create's first
  statement), creates are not inherited, `[operator("copy")]` takes one `const T&` (member-by-member copy
  when absent), a type with a `delete` and no copy is not copyable, fresh values relocate, `take(x)`,
  D-06 assignment, by-value parameters are destroyed by the callee, temporaries end with their full
  expression (a condition's before its body), a throwing create / copy / initializer list destroys the
  parts it built, `noexcept` stops the program. The raw-storage intrinsics `new (p) T(...)`, `destroy(p)`,
  `relocate(dst, src)` are in [builtins 14.1](../language/14-builtins.md#1-compiler-intrinsics).
- **Model:** `FunctionRecord::isNoexcept`, `synthesized` (`LifecycleOp`) and `HasBody(rec)` (a synthesized
  record has a body but no body node); `ProgramModel::EnsureSynthesizedLifecycle` / `SynthesizedLifecycle`
  (records `__c4_create/delete/copy_<T>_<n>`); `CheckLifecycleOperators` (`Model/OperatorAnnotations`)
  rejects `destroy`, `finalizer`, `move`, a `delete` with parameters and a wrongly typed `copy`.
- **HIR:** one subsystem owns object values - `LifecycleFacts`, `Lifecycle`, `MethodBody`, `CallArguments`
  (`InitialValue`, `BindArgument`), `Construction` (`PlanConstruction`), `Intrinsics` - described in
  [compiler/03](../compiler/03-hir.md#value-lifecycle). New code initializes objects only through
  `InitialValue` / `BindArgument` / `PlanConstruction` and assigns them only through `Lifecycle::Assign`;
  it never stores an object directly. Nodes: `SequenceNode::fresh`, `RelocateNode`, `NewNode::placement`,
  `DeferNode::exceptionalOnly`, `ThrowNode` destroy / copies. `SubobjectLValue` returns a `Base` view
  whenever the type changes, also at offset 0. Lowering diagnostics keep their source location
  (`CodeGenDriver::LoweringErrors`), every one is reported.
- **Codegen:** `EhTables` takes an `ObjectLifecycle` (CatchableType copy functions, ThrowInfo destructor);
  `RegionEmitter::EmitNoexceptBody` and `c4_noexcept_violation` (`C4Runtime/EHRuntime.cpp`); calls to
  `noexcept` callees are plain calls; exceptional-only cleanups; placement `new` and `Relocate` in
  `ObjectEmitter`; a declaration without an initializer re-zeroes its slot every time it runs
  (`IRContext::ResetSlot`); `!p` on a pointer is `p == null`.
- **Driver:** the default library is opt-in by include: only the Bedrock files a program includes (and
  what they include) are compiled (`SourceManager::RegisterIncludedLibraryFiles`).
- **Bedrock:** every container constructs, copies, moves and destroys its elements; `Vector`, `Queue`,
  `Stack`, `HashMap`, `String` and `StringBuilder` have copy operators and a valid zero state;
  `HashMap::Get` of a missing key is a zero value; `StringBuilder` number formatting handles every value.
- **Test runners:** the IsolatedTest runner kills a program after 60 s (a hang fails its case) and
  supports `mustContain`; both runners match expected text literally (brackets included).
- **Also fixed while there:** BUG-004, BUG-009, BUG-012, BUG-013, BUG-040, BUG-051, BUG-076, BUG-077 (lowering
  errors had no location), BUG-078 (StringBuilder formatting), BUG-079 (zero String / StringBuilder),
  BUG-080 (container element lifetimes), BUG-081 (`Template<Args>(...)` built the generic template),
  BUG-082 (`!p` inverted), BUG-083 (`a < F(x)` parsed as a template call), BUG-085 (every program compiled
  all of Bedrock).
- **Tests:** IsolatedTests `Lifecycle/` (ConstructionOrder, ExplicitBaseCreate, CopySemantics,
  TakeAndRelocation, Temporaries, ByValueParameters, Assignment, RangeForVariable, EarlyExits,
  ExceptionObjects, NoexceptTerminates, LoopLocalsStartZero, PlacementIntrinsics,
  AggregateConstructsOmittedFields, ConstructionExceptionSafety), `Templates/ConstructTemplateInstance`,
  `Operators/PointerLogicalNot`, `Bedrock/OptInDefaultLibrary`, `Interop/CppDestructorRuns`; NegativeTests
  `DestroyOperatorName`, `FinalizerOperatorName`, `MoveOperator`, `CopyOperatorSignature`,
  `DeleteOperatorParameters`, `CopyNonCopyable`, `PassNonCopyableByValue`, `NonCopyableMember`,
  `BaseWithoutDefaultCreate`, `CatchNonCopyableByValue`, `TakeOfRvalue`, `RelocateMismatchedTypes`,
  `RelocateFromConst`, `DestroyNonPointer`, `PlacementNewNonPointer`, `NoMatchingCreate`,
  `AggregateTooManyInitializers`; C4Testing `Semantic.Lifecycle` (10 tests), two Pratt disambiguation
  tests, the range-for HIR tests rewritten for the per-iteration variable; Bedrock
  `element_lifetime_tests.cf` and new String / StringBuilder cases.
- **Left for later, with owners:** moving a local at its last use (A10); `const` locals writable and `&x`
  of one mutable (BUG-084, A7); CBind overload names, static data members and shifted line numbers
  (BUG-086..BUG-088, B16); the rethrow re-test (BUG-089, B17). (A4 made template members on-demand, so a
  container of a non-copyable type compiles as long as nothing copies an element.)

### A10 - Last-use moves of locals (Done)
What changed, for the WPs that build on it:
- **HIR:** `Lifecycle::CopyOf` of a local's or by-value parameter's slot returns a `TransferNode` (the copy,
  recorded in `LoweringContext::transfers`); every variable's scope-exit cleanup is recorded
  (`NoteCleanup`, with the block a parameter or catch variable begins at). After a body lowers,
  `Backend/HIR/LastUseMoves` walks it in evaluation order and turns each transfer that is its variable's last
  use - no later use outside the other arm of an `if` / `?:`, no enclosing loop without the declaration, no
  escaped address (`AddressOfNode::transient` marks the addresses of reference arguments, a copy's source
  and discarded values), no use in another cleanup, no `goto` in the function - into a move: the object's
  bytes and a cleared drop flag. The flag is a bool slot set where the variable begins (`LocalDeclNode::liveFlag`,
  or the first statement of its block) and tested by its cleanup.
- **Codegen:** a `TransferNode` emits its `copy` (the move rewrites it); a declaration sets its drop flag.
- **Tests:** IsolatedTest `Lifecycle/LastUseMoves` (copy and destroy counts on every rule), C4Testing
  `Semantic.LastUseMoves` (9). Tests whose copy was a variable's last use (`Lifecycle/Assignment`,
  `ByValueParameters`, `CopySemantics`, `ExceptionObjects`, `ConstructionExceptionSafety`, Bedrock
  `container.lifetime.hashmap`) now use the variable afterwards to keep testing the copy, and test the move too.
- **Design note:** a move needs no zeroing of the source: the flag says the variable no longer owns the
  object, and the analysis guarantees nothing reads it again.

### A4 - Templates (Done)
What changed, for the WPs that build on it:
- **Language:** [templates](../language/08-templates.md) sections 1-5 are normative: arguments bind by
  position, value arguments are constant expressions, defaults (`Buf<>`, a default naming an earlier
  parameter), deduction from call arguments (D-17, written arguments are final), function templates in
  overload sets (a function written as it is beats an equal template instance), qualified and namespaced
  template calls, templated extension methods (D-21), out-of-line template methods matched by parameter
  types, members compiled on use (D-16), arrays decay to pointers.
- **Model:** `Model/Templates/` owns templates: `TemplateArguments` (`TemplateArg`, `TemplateArgs`,
  `TemplateArgumentResolver::Explicit / CompleteAndBind`), `TemplateDeduction` (the unifier),
  `FunctionTemplates` (`Collect` for `QueryFunction`, `AttachExtensions` for new instances; a template
  declared ahead of its definition is one function with it), `ObjectTemplateBody` (instance members).
  `ProgramModel::m_FunctionTemplates` maps a simple name to every template of that name;
  `FunctionRecord::templateOrigin` marks class-template members and function-template instances;
  `ObjectType::TemplateArgument::value` is a `ConstantValue`. `OverloadResolution` breaks ties toward
  non-template functions and costs array decay 1.
- **HIR / codegen:** `CodeGenDriver::EmitReachable` lowers template-made and synthesized records only when
  emitted code references them (`ModuleContext::TakeNewFunctions`); `LoweringContext::DecayArray`; one
  report per failed function (`LoweringContext::Fail`). MSVC names use back-references
  (`Backend/CodeGen/MsvcNames.cpp`).
- **Parser:** template argument lists parse constant expressions (`PrattParser::ParseTemplateArgumentExpression`),
  `<>`, parameter defaults, and `Ns::F<args>(...)` as a call.
- **Also fixed while there:** BUG-090 (arrays did not decay), BUG-091 (one failure reported twice).
- **Tests:** IsolatedTests `Templates/` (PositionalArguments, DefaultArguments, ArgumentDeduction,
  TemplateOverloads, QualifiedFunctionTemplate, OutOfLineOverloads, ExtensionTemplates,
  EnclosingParameterArgument, OnDemandMembers), `TemplateDeclBeforeDef` (declarations before definitions:
  deduced, same-arity overloads, extension and namespaced templates), `Pointers/ArrayDecay`; NegativeTests
  `NotATemplate`, `ConflictingDeduction`, `MissingTemplateArgument`, `TemplateValueForType`,
  `TemplateTypeForValue`, `TooManyTemplateArguments`, `UsedTemplateMemberIsChecked`,
  `ExtensionTemplateWrongReceiver`; C4Testing `Semantic.Templates` (8 tests), the MSVC back-reference test,
  `Semantic.HIRLowering.FailedInitializerIsReportedOnce`.
- **Left for later, with owners:** partial ordering of function templates (`template<T> F(T)` and
  `template<T> F(T*)` called with an `int32*` is ambiguous) and specialization (A8); variadic templates
  (A8); deduction from a target type (`funcptr<int32(int32)> f = Identity;`, A8).

### A5 - GC runtime (Done)
What changed, for the WPs that build on it:
- **Runtime:** `C4Runtime/GCRuntimeC.c` is replaced by `C4Runtime/GC/` (layout and behaviour in
  [runtime/01](../runtime/01-memory-and-gc.md#2-the-collector)): 64 MiB arenas with a two-level page map
  (O(1) lookup of any pointer, interior ones included), headerless objects in 72 size classes plus large
  objects, an explicit mark stack, ordered finalizers that run after the collection through
  `GC/GCFinalizerShim.cpp` (an escaping exception is fatal), counted pins that are roots, hash-map hook
  tables with no limit, pacing by live bytes (`c4_gc_set_percent`, 4 MiB minimum), returning idle pages to
  the OS, fatal out-of-memory. `GCPlatformWin32.c` is the only platform file (B31 adds a POSIX one; B29 added
  thread registration and per-thread caches, `GCThreads`). Raw memory and byte helpers moved to
  `C4Runtime/MemoryRuntimeC.c`.
- **C API changes:** `c4_alloc(size_t, size_t)` (codegen passed an `i32` align, BUG-092), new
  `c4_alloc_noscan`, `c4_gc_set_percent`, `c4_gc_is_reachable` returns `bool`, root enumerators receive
  `(user, visit, visitContext)`, `c4_gc_stats` gained `liveBytes` and `lastPauseNs`, `__c4_gc_roots` is an
  array of `{address, size}` ranges.
- **Codegen:** `TypeHandle::MayHoldPointers` decides noscan allocation and roots; `Backend/CodeGen/GcRoots.cpp`
  emits the roots table after every body (statics, static members, static locals, arrays and structs of
  pointers); the module carries the Windows x64 triple and data layout (BUG-097); declarations of one
  foreign symbol share one LLVM function or global (BUG-100); a `-test` build without `main` runs every step
  (BUG-096).
- **Driver:** `Compiler/RuntimeObjects.cpp` builds every source under `C4Runtime/` at `-O2 -Werror`, rebuilt
  when a source or header changes, written atomically (BUG-044, BUG-098). Builds print no warnings.
- **Bedrock:** `Memory/gc.ch` - the `GC` namespace over the whole C API.
- **Build:** Frost folder copies mirror their source, so files deleted from `Bedrock/` leave the shipped copy
  (BUG-099).
- **Also fixed while there:** BUG-008, BUG-092 .. BUG-100.
- **Tests:** IsolatedTests `MemoryGC/` (DeepListCollect, LargeAllocations, InteriorPointers, ManyHooks,
  PinnedChildren, FinalizerOrder, FinalizerException, StaticRoots, NoScanAllocations, AutomaticPacing,
  ExplicitDelete), `Interop/LinkSameSymbolTwice`; C4Testing `Semantic.TypeHandle` (3), five new
  `Semantic.CodeGen` tests, `Compiler.Runtime` (2); the new `assets/ToolsTest` suite (shipped Bedrock
  mirrors the source), now a gate.
- **Left for later, with owners:** threads (B29), Linux (B31), GC benchmarks (B32), `-gc-stats` (BUG-042,
  B21), relocatable runtime folder (BUG-043, B21), describing sub-objects of a custom allocation (roadmap
  item 5).

### A6 - Closures (Done)
What changed, for the WPs that build on it:
- **Parser:** `LambdaNode` keeps the capture list (`CaptureDefault` `[=]` / `[&]`, `LambdaCapture`: a variable,
  `&variable`, an init capture `n = expr` / `&r = expr`, `this`, `*this`) and `mutable`; `TryParse` scans to
  the matching `]` and needs `(` after it. `FunPtrTypeNode` parses `func<R(A)>` too (`func` is contextual,
  only before `<`), and both take trailing `*` / `&` / `[N]` / `const` layers (BUG-010).
- **Model:** `SignatureType` is the base of `FunPtrType` and the new `FuncType` (`TypeKind::Func`, 16 bytes,
  `TypeStore::Func`). `Model/Closures`: `ProgramModel::CreateClosure(spec)` builds a lambda's closure type
  (`__c4_closure_<function>_<k>`, fields `__capture_<name>`, a by-reference capture or `this` as a pointer, `*this` as
  a copy) and its call record (`[operator("call")]`, static when nothing is captured); `ClosureOf(type)`
  finds it. The call operator `()` maps to `call` in the operator table. `LoweringScopes::FindLocal` treats
  each Function layer as a frame boundary (BUG-101); `QueryVariable` never returns a frame variable from
  the global path. Only a pointer is a universal handle in overload ranking (BUG-102).
- **HIR:** `LambdaLowerer` (captures planned by `LambdaCaptures::FindOuterUses`, the call method lowered at
  once, the value a `Construct` of the closure); `Callables.cpp` converts a closure or funcptr to a
  `func<>` or funcptr target (`ConvertCallable`, used by `InitialValue`, so arguments, returns,
  assignments and declarations all convert); a callee that is a value (a variable, a field, a call result,
  a lambda) is called as one (`CalleeFromValue`: funcptr / func through `LowerIndirectCall`, an object
  through `operator_call`). New nodes `MakeFunc` and `FuncCode`.
- **Codegen:** `FuncEmitter`: a func is `{code, env}` called with the member ABI (env as `this`); a
  capturing closure's env is a GC copy (finalized by the closure's delete), a one-pointer closure's env is
  the pointer (`<call>_onepointer` adapter), a funcptr goes through `__c4_func_thunk_<sig>`.
- **Also fixed while there:**
  - BUG-010, BUG-101, BUG-102.
  - BUG-103: a method on a temporary that needs destruction.
  - BUG-104: field initializers run in every construction (`FieldInfo::m_Initializer`,
    `Lifecycle::InitializeField`; in a braced list without an object, `OmittedFieldValue`).
  - BUG-105: globals, static members and static locals start and end.
    - `__c4_global_init` is HIR (`GlobalInit.cpp`, through `DeclLowerer::StartGlobal`), in declaration order.
    - A static local's start is a `Once` region (thread-safe, abandoned on exception, re-entry is fatal).
    - Destruction at exit is `AtExit` + `C4Runtime/StaticStorageRuntimeC.c`; a constant static local needs
      no guard.
  - BUG-106: a `Block` step in a `Sequence`, so array copies and constructions work inside expressions
    (`Lifecycle::BuildTemporary`).
  - BUG-107: field designators parse and work in `T{...}`.
  - BUG-108: braced arrays inside `T{...}` / `T(...)` (`DeclLowerer::BracedValue`).
  - Braced arrays start their elements in index order: omitted elements are constructed, a range is
    evaluated once and copied, and naming an element or field twice is an error.
  - `LoweringContext::FailureNote` explains where an error happened.
- **Tests:**
  - IsolatedTests `Lambdas/` (CaptureByValue, CaptureByReference, CaptureDefaults, InitCaptures,
    CaptureThis, MutableLambdas, ClosureObjects, FuncValues, LambdaTemplates, NestedLambdas,
    CallOperator).
  - `Calls/FuncPtrTables`, `Calls/MethodOnConstructedTemporary`.
  - `Lifecycle/` (FieldInitializers, GlobalStarts, StaticLocals, ExitDestruction, StaticLocalReentry,
    ArrayValues, ArrayFills).
  - `Types/FieldDesignators`, `Types/BracedArrayValues`.
  - Negative tests: LambdaUncapturedLocal, CapturingLambdaToFuncptr, WriteValueCaptureWithoutMutable,
    CaptureNonLocal, CaptureThisOutsideMethod, CaptureNonCopyable, FuncSignatureMismatch,
    CallNonCallableObject, LocalFunctionUsesEnclosingLocal, BracedFieldInitializerUsesField,
    ArrayElementInitializedTwice, FieldInitializedTwice, RangeOfNonCopyable.
  - C4Testing `Semantic.Lambda` (8) and `Semantic.StaticStorage` (6).
- **Left for later, with owners:**
  - Deducing a lambda's or template's signature from a `func<>` target (A8).
  - Threads that call into a func's GC env concurrently: B29 made the collector safe for it; a `mutable`
    lambda's shared state is the program's to synchronize, as in C++.
  - A braced list still binds only to an `__init_list<T>` parameter of an ordinary call, by design
    (`f({1, 2})` against a `Point` parameter is no match; write `Point{1, 2}`).

### A7 - Const layers (Done)
What changed, for the WPs that build on it:
- **Parser:** a `const` qualifies the layer after it; one with no layer after it (`int32* const p`) and a
  const reference layer (`int32 const& r`) are errors. `QualTypeNode::DeclaresConstValue` says whether the
  declared entity is read-only; `VariableDefinitionNode::IsConst` means exactly that now (a `const int32* p`
  is not const). A method's trailing `const`, `noexcept` and `= 0` are independent flags (BUG-109); `**p`
  parses as a double dereference (BUG-113).
- **Model:** `TypeResolver::ApplyIndirections` keeps the constness of each layer's value (a pointer to a
  const array points at const). `FunctionRecord::isConst` (mangled `_const`, matched by out-of-line
  definitions and overrides) and `paramIsConst`; `MethodInfo::m_IsConst` is set at last. `LookupContext`
  carries the receiver's and the operand's `ReceiverAccess`; `SelectByReceiverAccess` keeps only const
  methods for a read-only receiver (`QueryError::ReadOnly`) and prefers the non-const twin for a writable
  one. A lambda's call operator is const unless `mutable`.
- **HIR:** `ReadOnly.cpp` (`ReadOnlyReason`, `AddressType`) is the one rule for what cannot be written;
  `LValue::readOnly` replaces `constRooted` and marks only const locals' slots and const-reference derefs,
  the rest is recognized from the lvalue (a const global, a Deref of a pointer to const, `this` in a const
  method, a const field outside its object's create / copy / delete, an element of a const array).
  Assignments, `++` / `--`, `take`, non-const reference binding (locals, parameters, captures) and
  reference returns check it; `&x` of a read-only `x` is a `const T*`. `PointerConversion.cpp` is the one
  pointer conversion rule, shared by assignment and `InitialValue` (declarations, arguments, returns).
- **Bedrock:** query methods are const; `Vector<T>` has const `At` / `Front` / `Back` / `Data` and const
  iteration (`ConstVectorIterator<T>`).
- **Also fixed while there:**
  - BUG-110: a pointer variable returned by reference returned its value.
  - BUG-111: a `const T*` could be returned as a `T*`.
  - BUG-112: declarations and returns converted between unrelated pointers.
  - BUG-114: unresolved declarations are reported at their location, with the reason.
- **Tests:**
  - IsolatedTests `Types/ConstLayers`, `OOP/ConstMethods`, `Pointers/PointerReferenceReturn`.
  - 20 negative tests, from `ConstLocalWrite` to `WriteThroughConstReferenceCapture`.
  - C4Testing `Semantic.ConstLayers` (6) and `Parser.FunctionDefinition.TestQualifiersCombine`.
- **Design note:** a by-copy capture is the lambda's own copy, so in a `mutable` lambda it is writable even
  when the variable it copies is const (C++ keeps such a member const); a pointer conversion needs identical
  pointees, so `T**` never becomes `const T**`.
- **Left for later, with owners:** `**` as the power operator (B8).

### A8 - Variadic templates and specialization (Done)
What changed, for the WPs that build on it:
- **Parser:** `PackExpansionNode` (`pattern...` in call, construction, `new` and declaration arguments,
  braced entries and template argument lists, through `PrattParser::ParseArgument`), `FoldExpressionNode`
  (the four fold forms, parsed after `(`), `sizeof...(X)` (`TypeOfExpressionNode::Kind::SizeOfPack`),
  `Ts... args` (`ParameterNode::IsPack`), a specialization's arguments after the name, a member template call
  with written arguments (`p.F<int32>(x)`), and a second template header for an out-of-line member template
  of a class template (`FunctionDefinitionNode::GetOwnerTemplateDefinition`).
- **Model:** packs are `SymbolInfo::packElements` (`TemplateArgumentResolver::PackSymbol`); `TemplateArgs` stay
  flat. `Model/Templates/Packs` (which packs a pattern names, expansion of template argument lists),
  `Specializations` (matching, most-specialized choice, explicit function specializations) and
  `PartialOrdering` (structural, with trailing packs; a top-level const is ignored). Overload resolution
  ranks equally good template instances by partial ordering (`TemplateOrdering`). `TemplateNameType` +
  `FunctionTemplates::ForTarget` deduce a function template from a target funcptr/func signature. Member
  function templates are `TemplateDef`s with `memberOf` (`Model/Templates/MemberTemplates`). Every class
  template instance has its own class layer (`InstanceScope`): its static data members live there and its
  members' symbols are named from it. `ConstEvaluator` folds `sizeof...`, folds, `&&` / `||`, and `sizeof(T)`
  in a template's bindings. The dead `TemplateParamType` / `TemplateSpecializationType` are gone.
- **HIR:** `PackElements`, `FoldLowerer` (`ExprLowerer::ApplyBinary` / `ApplyShortCircuit` are the shared
  operator rules), `TemplateBindings` (`BindTemplateParameters`, shared by bodies and global initialization),
  `ExprLowerer::ResolveFor` (a template name where a funcptr/func is expected) and `BindTemplateNames` (call
  arguments, after overload resolution). `Lifecycle::Discarded` is the one rule for a value evaluated for its
  effects. `ExprLowerer::ConstructionArguments` is the one construction-argument rule (the DeclLowerer copy is
  gone). `Type::member` resolves through the owner type (`StaticAccessOwner` / `StaticFieldValue`).
- **Codegen:** global initialization runs after the bodies, in rounds (`__c4_global_init_round<n>`, called by
  `__c4_global_init`), so a template instance made by any code has its static data members started;
  `MaterializeGlobals` runs after it.
- **Also fixed while there:**
  - BUG-115: a template instance's static data members had no storage and could not be named.
  - BUG-116: members of different template instances shared emitted symbols.
  - BUG-117: member function templates were rejected.
  - BUG-118: partial ordering compared a pattern's top-level const.
  - An unresolved alias target is reported at its location with the reason.
  - The gate script ran a stale C4Testing binary when the test build failed; it now reports the failure.
- **Tests:**
  - IsolatedTests `Templates/VariadicFunctions`, `FoldExpressions`, `VariadicClasses`, `Specializations`,
    `TargetTypeDeduction`, `MemberTemplates`, `InstanceStaticMembers`.
  - 19 negative tests, from `PackNotExpanded` to `MemberTemplateSpecializedInClass`.
  - C4Testing `Parser.Pratt.Packs` (10), `Parser.FunctionDefinition` (2), `Semantic.Templates.Variadic`,
    `Specialization`, `Deduction` (10), `Semantic.Templates.Members` (7), `Semantic.StaticStorage` (1).
- **Design notes:** a fold over `=` is rejected in favour of `((args = v), ...)`; a braced list is not an
  expansion pattern in arguments (`F({args...})` instead); a template instance's static data member that
  starts at run time is unordered relative to other globals, as in C++ (it starts before main).

### A9 - Multiple return values (Done)
What changed, for the WPs that build on it:
- **Parser:** a return type may be a return list (`QualTypeNode::ParseReturnList`, `IsReturnList`, elements
  that `ExpandsPack`) on functions, `funcptr` / `func` signatures and lambdas; `QualTypeNode::StartsReturnList`
  tells it from an annotation group (`[` ... `]` then a name and `(`). `ValueListNode` (`[e0, e1]`, an
  expression `[` that is not a lambda) and `DestructuringNode` (`[` ... `] =`, tried before declarations;
  elements are `Declare`, `Assign` or `Discard`).
- **Model:** `Model/Types/ReturnLists` interns one anonymous C-layout struct per list of element types
  (`ObjectType::IsReturnList`); `TypeResolver::ResolveReturnList` resolves a list in the asking bindings.
- **HIR:** `ExprLowerer::ResolveValueList` (target-typed in `ResolveFor`, or of its values' own types),
  `DestructuringLowerer`, `DeclLowerer::DeclareValue` (a local from a lowered value, sharing `BindSlot` with
  declarations), and assignment moved to `ExprAssignment.cpp` with `AssignTo` for an already lowered value.
  `LambdaCaptures` sees destructured declarations.
- **Codegen:** nothing new - a return list is a struct, returned by the existing C ABI rules.
- **Tests:**
  - IsolatedTest `Functions/MultipleReturns` (declare, assign, discard, swap, conversions, objects moved without
    copies, methods, templates, packs, funcptr, lambda).
  - 11 negative tests, from `ReturnListValueCount` to `DestructureIntoRvalue`.
  - C4Testing `Parser.ReturnLists` (9) and `Semantic.ReturnLists` (4).
- **Design notes:** a return list is written only as a return type (it can still be deduced into a template
  parameter); destructuring a list held by a variable copies it first.

---

## 5. Work packages for implementers

Order within this section is the recommended order. Each WP is self-contained; its dependencies
are listed. Paths are relative to the repo root unless they start with a `C4Lib/src/` subsystem
folder (`Parser/`, `Model/`, `Backend/`, ...).

### Lexer, parser, preprocessor

#### B1 - Operator precedence (C++ table)
- **Closes:** BUG-001. **Design:** D-27, [expressions 1](../language/10-expressions.md#1-precedence-normative).
- **Depends on:** none. **Status:** Done.
- **Done notes:** `BindPower` (in `PrattParser.h`) names the levels and `kInfixOperators[]` maps each
  infix token to `{power, rightAssociative}`; `is` / `as` share the relational level (C#), both
  parsed by `ParseTargetType`. Prefix operators and casts parse their operand at
  `BindPower::PrefixOperand` (`**`'s power), the `?:` middle operand at 0. Found and fixed BUG-119:
  the lexer combined operator characters across whitespace and comments (`* ==` became `*=` `=`);
  `Lexer::CombineAt` now requires adjacent characters and replaces the three copies of the combine
  loop. Tests: `PrecedenceTest` (7), `CompoundOperatorTest` Adjacency (3), TestCode
  `Operators/Precedence.cf`, negative `SpacedCompoundOperator`.
- **Steps:**
  1. Rewrite `PrattParser::InfixBindPower` (`Parser/PrattParser.cpp`) as one table (a `static constexpr`
     array of `{TokenType, bindPower, rightAssoc}`) matching the normative table: assignment and `?:`
     lowest (right associative), then `||`, `&&`, `|`, `^`, `&`, `== !=`, `is as < <= > >=`,
     `<< >>`, `+ -`, `* / %`, prefix unary, `**`, postfix.
  2. `**`: the right operand is parsed at prefix-unary power so `2 ** -1` works, and a prefix unary
     operator parses its operand at `**`'s power so `-x ** 2` is `-(x ** 2)`.
  3. Make sure assignment in expression position (`x = a && b`) parses through the same path as a
     declaration initializer.
- **Tests:** C4Testing `Parser/Expressions/PrecedenceTest.cpp`: one test per adjacent level pair
  (parse `a OP1 b OP2 c` in both orders, check the tree shape), associativity of `=`, `?:`, `**`, `-`.
  TestCode `Operators/Precedence.cf` evaluating every pair at runtime.
- **Docs:** `10-expressions.md` (delete section 2), `09-statements-and-control-flow.md` section 9.

#### B2 - Literals: suffixes, ranges, escapes, character family
- **Closes:** BUG-006, BUG-016, BUG-017. **Design:** D-02, [lexical 3.6-3.10](../language/03-lexical-structure.md).
- **Depends on:** B3 (for `char8`, done). **Status:** Done.
- **Done notes:** `Lexer/LiteralScanner.{h,cpp}` and `Lexer/QuotedLiteral.cpp` scan numbers, characters and
  strings whole (`SplitNumber` is shared with typing); a token carries `LiteralFacts` (unit width, first
  issue). Lexical errors go to `Lexer::Diagnostics()` keyed by token offset (an undo re-lex overwrites)
  and `CompilationUnit::ReportLexicalErrors` reports them before the parse errors. `Model/Literals.{h,cpp}`
  types numbers (`ParseNumericLiteral`, `std::from_chars`) and characters for both
  `ExprLowerer::ResolveLiteral` (`Backend/HIR/ExprLiterals.cpp`) and `ConstEvaluator::FoldLiteral`.
  Strings carry their width to `ModuleContext::StringGlobal` (one global per distinct units). Adjacent
  strings join. Found and fixed: BUG-121 (constant folding read `010` as 10 and `1e1` as 1), BUG-122
  (negative and zero extents), BUG-123 (`/*/` closed a comment, an unclosed `/*` was silent), and an
  embedded `\0` truncated a string. Tests: C4Testing `Lexer/NumericLiteralTest`, `Lexer/EscapeTest`,
  `Lexer/QuotedLiteralTest`, `Semantic/LiteralValueTest`, comment bounds; TestCode `Core/LiteralTyping.cf`;
  15 NegativeTests.
- **Steps:**
  1. Lexer (`Lexer/Lexer.cpp`): scan a numeric literal as `digits [. digits] [exponent] [suffix]` for
     every radix, then validate the suffix: integers `u U l L ll LL` and one `u` combined with `l`/`ll`
     in either order, floats `f F`; `1f` is an error (`write 1.0f`). Keep the token text whole.
  2. Literal typing (`Backend/HIR/ExprLowerer.cpp` `ResolveLiteral`; move it into a new
     `Backend/HIR/LiteralLowering.{h,cpp}`): parse with `std::from_chars` into `uint64` (never
     `stoll`), then type as C does: no suffix -> first of `int32, int64, uint64` that fits (decimal) or
     `int32, uint32, int64, uint64` (hex/binary/octal); `u` -> `uint32, uint64`; `l`/`ll` -> `int64`
     (`uint64` if too big); `ul`/`ull` -> `uint64`. Beyond `uint64` is the error
     `integer literal is too large`.
  3. Escapes: add `\a \b \f \v \e`, `\uXXXX` (exactly 4 hex digits), `\UXXXXXXXX` (exactly 8); in a
     `char8` string they encode as UTF-8, in `u"..."` / `U"..."` as UTF-16 / UTF-32. An unknown escape
     is an error naming it. `\x` takes at most 2 / 4 / 8 hex digits for char8 / char16 / char32.
  4. Prefixes: `u8"..."`, `u"..."`, `U"..."`, `u'x'`, `U'x'`; a multi-character literal `'ab'` is an
     error.
- **Tests:** C4Testing `Lexer/NumericLiteralTest.cpp`, `Lexer/EscapeTest.cpp`; TestCode
  `Core/LiteralTyping.cf` (sizeof and value of each suffix form, `0xFFFFFFFFFFFFFFFF` as `uint64`,
  `0xFFu`); NegativeTests `LiteralTooLarge`, `UnknownEscape`, `IntegerFloatSuffix`, `MultiCharLiteral`.

#### B3 - Built-in aliases `int`, `uint`, `char8`; remove `loop`
- **Closes:** BUG-050, BUG-052. **Design:** D-01, D-02, D-04.
- **Depends on:** none. **Status:** Done.
- **Done notes:** `TypeKind::Char8` (named `char8`); `TypeStore` maps `int`, `uint`, `char` to the
  `Int32` / `UInt32` / `Char8` singletons (`kBuiltinAliases`), so an alias IS its target: identical for
  overloads and sizeof, and the LSP's built-in coloring (it asks whether a name resolves to a
  primitive) covers the aliases with no LSP change. `loop` is gone from the lexer and the LSP keyword
  lists; keyword promotion is one table (`Token::PromoteKeyword`). `ForwardConstUse` and
  `ChainedForwardConstInit` compile once `int main()` is valid - correctly, file-scope declarations are
  order independent - so they moved to IsolatedTest `Types/ForwardFileScopeConsts`. `Benchmarks/gemm`
  is a gate (`run-benchmark.ps1 -Check`: builds C4 and C++, compares results). Found and fixed
  BUG-120: a duplicate definition is reported at the second body with the signature. Tests: TestCode
  `Types/BuiltinAliases.cf`, IsolatedTests `Types/LoopIsIdentifier`, `Types/ForwardFileScopeConsts`,
  negative `AliasOverloadRedefinition`.
- **Steps:**
  1. Rename `TypeKind::Char` to `Char8`; register built-in aliases `int -> int32`, `uint -> uint32`,
     `char -> char8` in `Model/BuiltinDeclarations.cpp` as real `AliasType`s (transparent).
  2. Lexer: drop the `loop` reservation (`Lexer/Token.cpp` `CHECK_RESERVED("loop", ...)` and
     `TokenType::Loop`).
  3. C4LSP semantic tokens: report every built-in alias with the built-in type token kind.
  4. Fix `assets/NegativeTest/ForwardConstUse`, `ChainedForwardConstInit`, `ConstPointerArraySize` to
     declare `int32 main()` and record what the compiler now reports (B24 sharpens the messages).
     Add `Benchmarks/gemm` to the gate build (it uses `int`, which is now legal).
- **Tests:** TestCode `Types/BuiltinAliases.cf` (`int`, `uint`, `char` are the same types as their
  targets: overload resolution, sizeof); IsolatedTest `Types/LoopIsIdentifier`.

#### B4 - Preprocessor completeness
- **Closes:** BUG-020. **Design:** D-44, [preprocessor](../language/02-preprocessor.md).
- **Depends on:** none. **Status:** Done.
- **Done notes:** the preprocessor is now `Preprocessor` (lines in `MacroPreprocessor.cpp`, directives in
  `MacroDirectives.cpp`) over `MacroText` scanning helpers, with `ConditionalStack`, `MacroExpression`
  (Pratt over int64, short-circuit aware) and `MacroExpander` / `MacroSubstitution` (token-wise bodies:
  parameters, `#`, `##`; arguments expanded first; `__FILE__` / `__LINE__`; argument-count errors). Each
  file returns `diagnostics`; `CompilerInstance::AddMacroDiagnostics` reports them first for the CLI and
  the LSP. `TEST` replaces `C4_TEST_MODE`; `PredefinedMacros` adds `PLATFORM_WINDOWS`, `ARCH_X64`,
  `C4_VERSION` (`Compiler/Version.h`, `--version`) and `-D`; Nuke passes its defines; the default
  library is a root of the pass. Found and fixed: BUG-124 (directives inside comments ran), BUG-125
  (parameters substituted one after another and inside literals), BUG-126 (`#define F (x)` was
  function-like), BUG-127 (calls across lines), BUG-128 (six IsolatedTests never ran their tests; the
  runner now fails a case with both `[test]` and `main`). `MacroPass::RunFile` was dead and is gone.
  Tests: C4Testing `Macro/MacroExpressionTest`, `MacroExpanderTest`, `MacroPreprocessorTest`; TestCode
  `Core/MacroConditions.cf`; IsolatedTests `Macros/PreprocessorIfDef` (now asserts every branch),
  `Stringize`, `TokenPaste`, `LineContinuation`, `PredefinedMacros`; 11 NegativeTests; ToolsTest
  `DefineFlag`, `VersionFlag`.
- **Steps:**
  1. New `Macro/MacroExpression.{h,cpp}`: a small Pratt evaluator over `int64` for `#if` / `#elif`:
     literals (with suffixes), identifiers (macro-expanded first; names still undefined are `0`),
     `defined(X)` / `defined X` (evaluated BEFORE expansion), `! ~ - +`, `* / %`, `+ -`, `<< >>`,
     `< <= > >=`, `== !=`, `&`, `^`, `|`, `&&`, `||`, `?:`, parentheses. Division by zero is an error.
     Replace `EvalCondition` in `Macro/MacroPreprocessor.cpp` with it.
  2. `\` line continuation for directives, `#error msg`, `#warning msg`, `#pragma` (ignored;
     `#pragma once` is a no-op because headers are parsed once anyway), `#line` is an error.
  3. `#` stringize and `##` paste in function-like macro bodies (C rules: operands of `#` / `##` are
     not pre-expanded).
  4. Predefined: `TEST` (=1 under `-test`; delete `C4_TEST_MODE` and update its users), `PLATFORM_WINDOWS`
     (=1), `ARCH_X64` (=1), `C4_VERSION` (`major*10000 + minor*100 + patch`), `__FILE__` (string
     literal), `__LINE__`.
  5. `-D NAME` / `-D NAME=VALUE` in `Compiler/BuildUtils.cpp` `ParseArgs`, applied to every unit as
     predefined macros; Nuke passes project defines through it (`Nuke/ObjectBuilder.py`).
  6. Fix `assets/IsolatedTest/Macros/PreprocessorIfDef` so it exercises a real `&&`.
- **Tests:** C4Testing `Macro/MacroExpressionTest.cpp` (every operator, precedence, `defined` forms,
  undefined name is 0, `#define X 0` then `#if X` is false); TestCode `Core/MacroConditions.cf`;
  IsolatedTests `Macros/Stringize`, `Macros/TokenPaste`, `Macros/LineContinuation`,
  `Macros/PredefinedMacros`; NegativeTests `PoundError`, `IfDivideByZero`.

#### B5 - Default arguments, `sizeof(expr)`, `alignof`, `offsetof`
- **Design:** D-41, [functions 7.4](../language/07-functions-and-lambdas.md#74-default-arguments).
- **Depends on:** none. **Status:** Done.
- **Done notes:** defaults parse as full expressions. `ExprLowerer::DefaultArgument` lowers one in the
  callee's declaration context (a fresh frame hides the caller's locals; the callee's scope and unit; no
  receiver) for calls, creates and `Lifecycle::DefaultArguments`. A create whose parameters all have
  defaults is the default create. `TypeOfExpressionNode` parses its own forms (a parenthesized name keeps
  both readings); `Model/CompileTimeQueries` measures size, alignment and field-path offsets for
  `Backend/HIR/ExprQueries.cpp` (an expression operand lowers into a scratch tree, so nothing is emitted)
  and `Model/ConstQueries.cpp` (constants type variables, elements, fields, dereferences, literals and
  casts). Found and fixed: BUG-129 (a default bound its names at the call), BUG-130 (an all-defaulted
  create was not the default create). Found and handed to B11: BUG-131 (initialization and assignment
  skip the conversion rules). Tests: TestCode `Core/DefaultArgumentExpressions.cf`, `Core/SizeofForms.cf`;
  C4Testing `Parser/Expressions/CompileTimeQueryTest`; NegativeTests `OffsetofUnknownField`,
  `OffsetofStaticField`, `OffsetofNotAnObject`, `SizeofVoid`, `DefaultArgumentNamesParameter`.
- **Steps:**
  1. `Parser/Nodes/Declarations/ParameterNode.cpp`: parse the default with `PrattParser::ParseExpression`
     at assignment power (it stops at `,` and `)`), not a single token.
  2. HIR already lowers defaults per call; test that a default may name a global, an enum member,
     `sizeof(T)`, a negative literal and a function call.
  3. `sizeof(expr)` for any expression: lower the operand into a throwaway arena (unevaluated: nothing
     is emitted) and take its type's size; a reference gives the referent's size. Add `alignof(T)` /
     `alignof(expr)` and `offsetof(T, field)` (`int64` constants; a static or unknown field is an error).
- **Tests:** TestCode `Functions/DefaultArgumentExpressions.cf`, `Core/SizeofForms.cf`; NegativeTests
  `OffsetofUnknownField`, `DefaultArgumentNotConvertible`.

#### B10 - Switch fallthrough rule and comma in `for` steps
- **Design:** D-39, D-40. **Depends on:** A1 (Done). **Status:** Done.
- **Done notes:** `Backend/HIR/Reachability.{h,cpp}` answers "can this statement list complete normally"
  over HIR (break / continue targets by label, do-while, endless loops, switch, try, labels revive) - B15
  reuses it. `StmtLowerer::LowerCase` lowers one case: a non-empty case that can complete is an error
  unless it is the last; `fallthrough;` must end its case and needs a next case. `ForLoopNode::GetSteps()`
  replaces `GetIncrement()`; `LoopLowerer::LowerSteps` runs each step as its own full expression. No suite
  relied on implicit fallthrough; two C4Testing HIR tests that encoded it now use `fallthrough;`. Tests:
  TestCode `ControlFlow/SwitchStackedLabels.cf`, `ControlFlow/ForCommaStep.cf`; C4Testing
  `Semantic/ReachabilityTest`; NegativeTests `SwitchImplicitFallthrough`, `SwitchFallthroughOnOneBranch`,
  `FallthroughNotLast`, `FallthroughInLastCase`.
- **Steps:**
  1. `StmtLowerer::LowerSwitch`: a non-empty case whose end is reachable is an error with the fix-it
     text (use B15's reachability helper, or write it here first). Stacked empty labels are allowed.
  2. Parser: the `for` step is a comma-separated expression list (`ForLoopNode`); HIR lowers it in order.
  3. Add `fallthrough;` wherever the suites relied on implicit fallthrough.
- **Tests:** NegativeTest `SwitchImplicitFallthrough`; TestCode `ControlFlow/SwitchStackedLabels.cf`,
  `ControlFlow/ForCommaStep.cf`.

### Types and semantics

#### B6 - Visibility and access checking
- **Closes:** BUG-023, BUG-024. **Design:** D-28, [objects 5.7](../language/05-objects.md#57-visibility).
- **Depends on:** none. **Status:** Done.
- **Done notes:** `Backend/HIR/AccessCheck.{h,cpp}`: `Check(ctx, Member)` decides from the class scopes the
  lowered function sits in (`ctx.scope` and its parents; `Scope::objectType`, now set for template-instance
  scopes too), so static methods, nested types, lambdas, field initializers and default arguments get the
  right domain and extension methods (namespace scope) are outside code. `CheckCall(ctx, record)` uses the
  new `FunctionRecord::access`; `CheckField(ctx, owner, field)` the field's. Called from implicit-this and
  `obj.f` fields, `obj.s` / `T::s` statics, named calls, `EmitOperatorCall`, `[]=` setters, cast methods,
  the range-for protocol, callable conversions and `PlanConstruction` (every create, including the default
  one). Template instances now record their fields' and methods' access (they were all public).
  `ObjectDefinitionNode::ApplyAccessSection` reports a missing or conflicting modifier and the interface
  rule; an interface label other than `public:` is an error. Default construction checks the 0-argument
  create everywhere it runs (`AccessCheck::CheckDefaultCreate`: `T v;`, arrays, omitted braced fields,
  `Lifecycle::ConstructField` for a create's fields - located at the field, which now records its declaring
  node - and the base). BUG-132 fixed on the way: a compiler-written create builds an argument-only base like
  `Base b;` (it errored, without a location, only when the base had field initializers). Bedrock's
  `hashmap.ch` and `dom.ch` gained the modifiers. Tests: TestCode `OOP/ProtectedFromDerived.cf`; NegativeTests `PrivateMethodCall`,
  `ProtectedFieldOutside`, `ProtectedMethodOutside`, `PrivateCtor`, `PrivateCtorNew`, `PrivateDefaultCreate`,
  `ExtensionReadsPrivate`, `DerivedReadsBasePrivate`, `PrivateStaticOutside`, `PrivateOperatorOutside`,
  `SectionMissingModifier`, `SectionMissingModifierMethod`, `InterfacePrivateMember`,
  `InterfaceProtectedSection`, `PrivateCreateField`, `PrivateCreateArray`, `PrivateBaseCreate`,
  `PrivateCreateOmittedField`; IsolatedTests `Lifecycle/ExplicitBaseCreate` (BUG-132).
- **Steps:**
  1. `ObjectDefinitionNode::ApplyAccessSection`: under a section label a member with no inline
     modifier is the error `member 'x' is under 'public:' but has no access modifier; write 'public'`.
     Nested type declarations stay exempt.
  2. New `Backend/HIR/AccessCheck.{h,cpp}` with one function that takes the lowering context, the
     owning type, the member's access and kind and name, and returns `QueryResult<void>`: public always;
     private only inside the owner's own methods (and methods of types nested in it); protected inside
     the owner's and derived types' methods. Extension methods are outside code. Call it from field
     access (`ResolveMemberAccess`), method calls (`ResolveCall`), operator calls (`EmitOperatorCall`),
     constructors (`T v(...)`, `T(...)`, `new T(...)`) and static member access.
  3. Interface members are public; an explicit `private` / `protected` there is an error.
- **Tests:** NegativeTests `PrivateMethodCall`, `ProtectedFieldOutside`, `ProtectedMethodOutside`,
  `PrivateCtor`, `ExtensionReadsPrivate`, `SectionMissingModifier`, `InterfacePrivateMember`;
  TestCode `OOP/ProtectedFromDerived.cf` (the allowed cases).

#### B7 - Modifiers with meaning
- **Closes:** BUG-026, BUG-030. **Design:** D-29.
- **Depends on:** A2 (abstract checks read the new slot table). **Status:** Done.
- **Done notes:** `Model/Abstractness.{h,cpp}` answers "can an object of T exist" (declared abstract, an
  interface, or a vtable slot whose final overrider has no body and is not linked) and words the by-value
  and creation errors. The model checks fields (all, with the field's location) and globals in
  `ModelBuilder::CheckObjectTypes`; HIR checks value locals (`DeclLowerer::LowerLocalDecl`), signatures
  (`LowerFunction`), `PlanConstruction` and `new`. `final`: `ObjectDefinitionNode::ParseClassModifiers`
  (prefix `abstract` / `final`, conflicts reported by `CheckClassModifiers`), `ObjectType::SetFinal`,
  `ClassHierarchy::CheckBaseList`, `MethodInfo::m_IsFinal`, `VirtualTables::Overridden` and
  `CheckFinalMethods`; a trailing `final` parses like `override`. An `abstract` method is virtual and pure
  (`FunctionDefinitionNode::IsVirtual/IsPureVirtual`); a body or `static` is an error. Free functions and
  extensions reject `virtual` / `override` / `abstract` / `final`. `volatile`: `SymbolInfo::isVolatile`,
  `FieldInfo::m_IsVolatile`, `HIRFunction::Slot::isVolatile`; codegen's `ExprEmitter::AccessOf` makes
  every load and store of a volatile location (or part of one) volatile. `thread_local`: the same three
  carry `isThreadLocal`; codegen sets LLVM `thread_local`; B7's rules (no managed pointer, no lifecycle, a
  constant start) were lifted by B29, which removed `Backend/HIR/ThreadLocalStorage` and
  `CheckThreadLocalGlobals`; a thread_local local has static storage; such variables skip reflection and the
  `__c4_gv_` companions. `concurrent` is an error. Tests: TestCode `OOP/AbstractAndFinal.cf`; IsolatedTests
  `Types/VolatileStore`, `Types/ThreadLocalCounter` (a real second thread), checked by the new
  `ExpectedIR.json` manifest; C4Testing `Parser.ObjectDefinition` / `Parser.FunctionDefinition` modifier
  tests; 28 NegativeTests (`NewAbstract`, `AbstractValueLocal`, `PureVirtualNotImplemented`,
  `DeriveFromFinal`, `OverrideFinal`, `ThreadLocalManagedPointer`, `ConcurrentModifier`, ...).
- **Steps:**
  1. `abstract class` / `abstract struct`: `ObjectType::SetAbstract`; HIR rejects a value local, a
     by-value parameter / return / field, `new T` and `T(...)` of an abstract type or of a type with an
     unimplemented pure virtual (`'T' is abstract: 'Area' is not implemented`). A slot is unimplemented
     when `VirtualTables::FinalOverrider(T, loc, slot)` over every `VptrLocations(T)` entry names a
     method whose record has neither `bodyNode` nor `isLinked` (the case `VtableEmitter` fills with
     `c4_purecall`).
  2. `final` on a class: deriving from it is an error; on a method: overriding it is an error.
  3. `noexcept`: parse and store on the record (A3 owns the codegen).
  4. `volatile` variables/fields: `LValue::isVolatile`; codegen emits volatile loads/stores.
  5. `thread_local` globals/statics: LLVM `thread_local`; error if the type contains a managed pointer
     (`thread_local storage of managed pointers needs threading support`). Lifted by B29.
  6. `concurrent`: error `'concurrent' is not a C4 modifier`.
- **Tests:** NegativeTests `NewAbstract`, `AbstractValueLocal`, `PureVirtualNotImplemented`,
  `DeriveFromFinal`, `OverrideFinal`, `ThreadLocalManagedPointer`, `ConcurrentModifier`; IsolatedTests
  `Types/VolatileStore` (the `.ll` has `store volatile`), `Types/ThreadLocalCounter`.

#### B8 - Operator table completion
- **Closes:** BUG-005, BUG-011, BUG-029. **Design:** D-23..D-26 (D-03 was done by A3).
- **Depends on:** A3, A6 (both Done). **Status:** Done.
- **Done notes:** `OperatorTable::CanonicalAnnotation(token, parameterCount)` binds a two-meaning token by
  arity; the catalog gained `**` `power`, `<<=` / `>>=`. `CollectOperatorAnnotations` takes the parameter
  count; `MethodInfo::m_OperatorAnnotations` (with `HasOperator`) keeps every name. `rightIsReceiver` is
  gone: `SymbolQueries::QueryOperator` looks on the left operand's type only, keeps only methods that can
  take the right operand (so a non-viable candidate is no longer picked) before the const filter, and
  reports ambiguity; a primitive left operand with an object right operand searches the new
  `ProgramModel::m_PrimitiveExtensions` index (`FindCastMethod` uses it too). `EmitOperatorCall` binds the
  left operand as the receiver (`Lifecycle::AddressableObject` now makes a temporary for a primitive). The
  call operator already worked (A-packages); it gained a test. `**`: `IntrinsicOp::Pow`, codegen
  `llvm.pow` for floats and `CodeGen/IntegerPower` (one internal square-and-multiply function per integer
  type), `ConstEvaluator` folds it. A free `[operator]` function is an error. The `[]=` setter is found by
  arity (`FindOperatorMethod(..., "index_assign", 2)`). Found and fixed on the way: BUG-133 (the built-in
  operators never checked operand kinds: float `%` / bitwise / shifts, pointer `*` / `-p`, float and
  pointer `++`, and `p += n` all failed IR verification; `Backend/HIR/BuiltinOperators` holds the operand
  rules, float `%` is `frem`, `ExprLowerer::Stepped` steps by an element, compound assignment lowers
  through `ApplyBinary`) and BUG-134 (a template's bare name inside its body is the instance). Tests:
  C4Testing `Semantic/OperatorTableTest`; TestCode `Operators/Power.cf`, `Operators/FloatRemainder.cf`,
  `OperatorOverloads/UnaryTokens.cf`, `ShiftAssign.cf`, `ScalarLeftExtension.cf`, `CallOperator.cf`,
  `Types/InjectedClassName.cf`; 13 NegativeTests (`FreeOperatorFunction`, `ScalarLeftNoOperator`,
  `RightOperandNotReceiver`, `FloatBitwiseAnd`, `PointerMultiply`, ...).
- **Steps:**
  1. `Model/OperatorTable.cpp`: canonicalize a token by the method's parameter count (unary vs
     binary); add `call` (`"()"`), `shift_left_assign`, `shift_right_assign`, `power`.
  2. Resolvers (`Model/Builder/*Resolver.cpp`): record every annotation name, not just `[0]`.
  3. Delete the right-operand receiver swap (`rightIsReceiver` in the model's operator lookup and in
     `ExprLowerer::EmitOperatorCall`). Add extension methods on scalar types where Bedrock and the
     suites relied on the swap.
  4. Call operator: in `ExprLowerer::ResolveCall`, a callee that is a VALUE of object type with a
     `call` operator dispatches it.
  5. `**` on numerics: integers lower to a new `IntrinsicOp::Pow` (codegen: square-and-multiply loop;
     a negative exponent gives 0 except for bases 1 and -1), floats to `llvm.pow`.
  6. A free `[operator]` function without `extends`: error `operators must be methods or extension methods`.
- **Tests:** C4Testing `Semantic/OperatorTableTest.cpp` (arity mapping, multi-name registration);
  TestCode `OperatorOverloads/UnaryTokens.cf`, `OperatorOverloads/CallOperator.cf`,
  `OperatorOverloads/ShiftAssign.cf`, `Operators/Power.cf`, `OperatorOverloads/ScalarLeftExtension.cf`
  (`2.0f - v` through an extension on float is right); NegativeTests `FreeOperatorFunction`,
  `ScalarLeftNoOperator`.

#### B9 - Type-former gaps
- **Closes:** BUG-018, BUG-019 (BUG-010 was fixed by A6). **Design:** D-33, [types 4.5-4.6](../language/04-types.md).
- **Depends on:** A7 (same code in `TypeResolver`; Done). **Status:** Done.
- **Done notes:** `TypeResolver::ApplyIndirections` rejects an array layer with no extent with the D-33
  fix-it (the layer used to be ignored, so `int32[] a` was an `int32`), and a non-constant extent now names
  the expression. BUG-018 and BUG-019 re-tested: both work since the HIR rewrite (reads, writes, compound
  assignment and range-for through `int32[4]& f()`; `&r` of reference locals); entries deleted with the
  new tests. Tests: TestCode `Types/FuncPtrTables.cf`, `Types/RefToArrayIndex.cf`,
  `Types/AddressOfReferenceLocal.cf`; NegativeTests `EmptyArrayExtent`, `EmptyArrayParameter`,
  `EmptyArrayField`, `NonConstLocalArraySize` (and sharper `NonConstArraySize`, `ConstPointerArraySize`).
- **Steps:**
  1. (Done in A6: `funcptr<...>` and `func<...>` take their trailing layers.)
  2. `T[]` is an error (D-33).
  3. Re-test BUG-018 (`int32[4]& f(); f()[i]`, read and write) and BUG-019 (`S& r = s; S* q = &r;`);
     fix what is wrong, then delete both entries with the new tests.
- **Tests:** TestCode `Types/FuncPtrTables.cf`, `Types/RefToArrayIndex.cf`,
  `Pointers/AddressOfReferenceLocal.cf`; NegativeTest `EmptyArrayExtent`.

#### B11 - Conversions: `enum class`, const references
- **Closes:** BUG-131. **Design:** D-32. **Depends on:** A3 (done). **Status:** Done.
- **Done notes:** `Backend/HIR/ImplicitConversion.{h,cpp}`: `Scalar` (numbers, plain enums, the D-32 enum
  class errors, the constant 0 and arrays to pointers, a pointer's truth value, init lists to
  `__init_list<T>*`) and `ObjectProblem` (same type, a derived object sliced to its base, a swizzle run as
  its packed vector; otherwise the error with the explicit form to write). `CallArguments::ConvertedValue`
  is the one conversion, `InitialValue` words it for initializations; `ExprLowerer::AssignedValue` /
  `AssignedPart` for assignments (lifecycle assignments slice too); `StmtLowerer::LowerReturnValue` and
  `IndirectCall` reuse it. `BindArgument` converts a `const T&` argument into a temporary. `ParamCost`:
  no integer to or from an enum class, a non-const reference binds only its own type (or derived), a
  derived object binds a by-value base. Found and fixed on the way: BUG-135 (a derived object stored its
  whole value into a base variable, overrunning it) and BUG-136 (a conversion to bool truncated: `bool b
  = 2` was false). No Bedrock or suite call site passed integers to scoped enums; one IsolatedTest stored
  an integer into a pointer field and was corrected. Tests: TestCode `Types/ConstRefConversion.cf`; 18
  NegativeTests (`IntToEnumClass`, `DefaultArgumentNotConvertible`, one per BUG-131 form, ...).
- **Steps:**
  0. One implicit-conversion check (docs/language/04 4.9) for every place a value becomes a typed
     location: initialization (`InitialValue`), arguments (including defaults), assignment and return.
     Pointer <-> integer, pointer <-> floating point, an object <-> a scalar with no create or cast, and an
     integer to a pointer (write `null`) are errors that name both types.
  1. `Model/OverloadResolution.cpp` `ParamCost`: integer -> `enum class` is not viable; plain `enum`
     keeps cost 1. Same rule in `LoweringContext::CoerceValue` for stores.
  2. A `const T&` parameter accepts any argument convertible to `T`: HIR converts into a temporary
     (destroyed at the end of the full expression by A3's temporary rules). Cost = the conversion cost.
  3. Fix Bedrock / suite call sites that passed integers to scoped-enum parameters.
- **Tests:** NegativeTests `IntToEnumClass`, `DefaultArgumentNotConvertible`, one per BUG-131 form;
  TestCode `Types/ConstRefConversion.cf`.

#### B12 - `va_list`
- **Design:** D-35. **Depends on:** none. **Status:** Done.
- **Done notes:** `va_list` is a built-in alias of `char8*` (`TypeStore`). `Parser/Nodes/Expressions/
  VaArgExpressionNode` parses `va_arg(list, T)` (dispatched from `PrattParser` next to `sizeof`).
  `Backend/HIR/VarArgs.{h,cpp}` lowers the intrinsics to the new `VaNode` (Start / End / Copy / Arg, over the
  address of the va_list variable): `va_start` only in a variadic function, `va_arg` rejects the promoted
  narrow types with the type to read. `ExprEmitter::EmitVa` emits `llvm.va_start` / `va_end` / `va_copy` and
  the Win64 slot walk (an object that is not 1/2/4/8 bytes arrives by pointer, as the caller passes it).
  The `va_arg_int` family and its codegen are gone; Bedrock `Text/format.cf` and the suites use `va_arg`.
  BUG-137 fixed on the way: CBind dropped the `...` of C variadic functions. Tests: TestCode
  `Core/VaListForms.cf` (int32, int64, double, pointer, an 8-byte and a 24-byte struct, a promoted float,
  `va_copy`); IsolatedTest `Interop/VarargsToC` (C variadics from C4, a C4 variadic called from C);
  NegativeTests `VaStartNotVariadic`, `VaArgFloat`, `VaArgSmallInteger`, `VaArgNotAList`, `VaArgIntRemoved`.
- **Steps:**
  1. Built-in type `va_list` (an alias of `char8*` on Win64).
  2. Intrinsics `va_start(ap)`, `va_end(ap)`, `va_copy(dst, src)`, `va_arg(ap, T)`: the parser reads
     `va_arg`'s second argument as a type (like `sizeof`); HIR emits a new `VaArgNode`; codegen
     lowers it the way clang does for x86_64-pc-windows-msvc (8-byte slots; aggregates that are not
     1/2/4/8 bytes arrive by pointer).
  3. Remove `va_arg_int / _uint64 / _double / _float` and the `void*` list form everywhere.
- **Tests:** TestCode `Functions/VaListForms.cf` (int32, int64, double, pointer, an 8-byte struct, a
  24-byte struct, `va_copy`); IsolatedTest `Interop/VarargsToC` (C4 variadic called from C and C
  variadic called from C4).

#### B13 - `packed T[N]` as a value type
- **Design:** D-36, D-37. **Depends on:** none. **Status:** Done.
- **Done notes:** `QualTypeNode` reads a `packed` prefix; `TypeResolver::ApplyIndirections` turns a packed
  first array layer into the interned `TypeStore::Swizzle(T, N)` (named `packed T[N]`), checked by
  `SwizzleType::WhyNotAVector` (also used for lane views). HIR: `VectorLaneDyn` indexes any packed value,
  `DeclLowerer::FillLanes` and `ExprAssignment::AssignLanes` fill and assign braces, an array view
  (`ObjectType::FindPackedArrayAlias`) is the whole vector as a value, and `ExprLowerer::VectorBinary` does
  lane-wise operators with scalar broadcast and `packed bool[N]` comparison masks (`BuiltinOperators` rules
  for masks; `!` is lane-wise on a mask). D-37 errors in `ExprLowerer` (`&` of a lane or run),
  `ExprAssignment::CheckWritable` (repeated lanes) and `SwizzleViewNode` (one-character lanes). Lane indices
  are per lane-view clause, so `packed float x, y; packed float r, g;` alias. Found on the way and fixed:
  BUG-138 (conditions were never type-checked: a struct, an array, a void call or a mask compiled, crashed
  or failed LLVM verification; `ImplicitConversion::ConditionProblem` is now the one rule for `if`, loops,
  `?:`, `&&` and `||`), BUG-139 (`&&` on masks crashed), BUG-140 (packed-type diagnostics), BUG-141
  (`switch` on a struct, float or pointer crashed) and BUG-142 (case labels were not checked against the
  selector: `Backend/HIR/CaseLabels.{h,cpp}` checks type, range, duplicates and enum classes; a switch has
  one default). Tests: TestCode `Types/PackedArrayValues.cf`, `ControlFlow/SwitchLabels.cf`,
  `ControlFlow/ConditionTruthValues.cf`; NegativeTests `AddressOfLane`, `AddressOfSwizzle`,
  `AddressOfVectorIndex`, `RepeatedLaneWrite`, `MultiCharLaneName`, `PackedTooWide`, `PackedTooManyLanes`,
  `PackedOneLane`, `PackedWithoutArray`, `PackedParamWithoutArray`, `PackedLaneViewOutsideUnion`,
  `PackedPointerElement`, `PackedNotNumber`, `PackedLaneCountNotConstant`, `PackedVectorMismatch`,
  `PackedElementMismatch`, `PackedMask*`, `NotOnFloatVector`, `Condition*`, `*ConditionStruct`,
  `LogicalOrStruct`, `SwitchOn*`, `SwitchCase*`, `SwitchDuplicateCase`, `SwitchTwoDefaults`,
  `SwitchEnumClassIntCase`, `SwitchIntEnumClassCase`. Found while refreshing a downstream engine and fixed after the
  B13 commit: BUG-143 (constants resolved from the root, not where they are written: `ConstEvaluator` takes
  the scope it folds in, `TypeResolver::FindVisibleQualified` resolves a qualified name from the context
  first), BUG-144 (an unfolded `const` was an error; it now starts at runtime, and
  `ConstEvaluator::FoldSymbol` folds a constant from its declaring scope, reporting loops by symbol),
  BUG-145 (packed vectors laid out differently from LLVM: alignment is the largest power of two dividing
  the size, `ModuleContext::Storage` gives non-power-of-two vectors `[N x T]` storage in aggregates and
  `CodeGen/VectorAlignment` aligns their accesses) and BUG-146 (`.5f` now says it needs a leading digit).
  Tests: TestCode `Core/ScopedConstants.cf`, `Core/RuntimeConstants.cf`, `Types/PackedLayout.cf`;
  NegativeTests `ConstantCycle`, `ConstantSelfReference`, `RuntimeConstantAsExtent`, `UnknownNameInConstant`,
  `FloatWithoutLeadingDigit`; C4Testing `NumericLiteralTest`.
- **Steps:**
  1. Type grammar: `packed T[N]` in any type position resolves to the interned `SwizzleType`,
     validated (2..16 lanes, at most 64 bytes, numeric element).
  2. HIR: indexing (`VectorLaneDyn` on any lvalue), braced init, elementwise `+ - * / %`, comparisons
     (lane-wise, result `packed bool[N]`), scalar broadcast, unary `-`; assignment between a packed
     value and a class's packed array alias (`this.data = v`).
  3. ABI: passes and returns in XMM like a pure packed vector (`Abi` already handles `<N x T>`).
  4. D-37 errors: `&v.x`, `&v.xy`, repeated-lane writes, multi-character lane names.
- **Tests:** TestCode `Types/PackedArrayValues.cf` (a Vec4 constructor taking `packed float[4]`,
  arithmetic, comparisons, passing and returning); NegativeTests `AddressOfLane`, `AddressOfSwizzle`,
  `RepeatedLaneWrite`, `MultiCharLaneName`, `PackedTooWide`.

#### B14 - Normalized integer semantics
- **Design:** D-38. **Depends on:** none. **Status:** Done.
- **Done notes:** `Backend/CodeGen/NormalizedConversion.{h,cpp}` replaces `EmitNormalizeClamp`: float to
  normalized (clamp, scale, `llvm.rint` ties to even, NaN to 0), normalized to float (snorm clamped at -1),
  normalized to normalized by value, and the integer encoding clamp; `RawOf` encodes constants for
  `ModuleContext::NumberConstant`, which is now the one constant rule for expressions and global starting
  values. `Type::UsualArithmeticResultKind` gives a normalized operand's float type (double for 32/64 bits
  or a double operand), so HIR arithmetic, comparisons and unary minus run on values. Bit operators, `~`,
  `++`/`--`, `switch` and case labels reject normalized values; `ConstEvaluator::FoldSymbol` does not fold
  other constants over a normalized one. Integers stay the encoding (`IsolatedTest/Types/NormalizedIntClamp`
  passes). Tests: TestCode `Types/NormalizedIntegers.cf`; NegativeTests `NormalizedIncrement`,
  `NormalizedBitAnd`, `NormalizedShift`, `NormalizedBitNot`, `NormalizedSwitch`, `NormalizedConstantInExtent`.
- **Steps:** conversions in `ExprEmitter::EmitConvert` / `EmitNormalizeClamp` (float <-> snorm/unorm
  with scale, round to nearest, clamp); HIR arithmetic on normalized operands converts both to the float
  type first (`ExprLowerer::ResolveBinary`), and the result is that float type.
- **Tests:** TestCode `Types/NormalizedIntegers.cf` (each width, both directions, clamping, the -1
  edge, arithmetic result type); `IsolatedTest/Types/NormalizedIntClamp` keeps passing.

#### B15 - Range-for by reference, missing-return detection
- **Design:** D-54, D-55. **Depends on:** A1 (Done). **Status:** Done.
- **Done notes:** `LoopLowerer::DeclareLoopReference` binds a `T&` / `const T&` loop variable to the element's
  address (the slot of a reference local), checking the element is a location of type `T` and that a
  mutable reference does not name a read-only element; a by-value `dereference` is reported with the fix.
  `LoweringDriver` runs the existing `Reachability::CompletesNormally` over each value-returning body
  (`main` exempt) and reports `control reaches the end of non-void function 'F'` at the closing brace;
  `FunctionEmitter` ends leftover blocks with `unreachable` (`ret 0` in `main`). Fixed on the way: BUG-147 (a
  function named `main_`-something could be picked as the entry point: `IsEntryPoint` now asks for the
  file-scope free function named `main`) and BUG-148 (a braced list assigned to an object or array failed with
  "expression not yet lowered" at the wrong line: `ResolveFor` builds it against the target, and an untyped
  braced list says so at its own location; `BracedInitializerNode::ToString` prints entries as written).
  Tests: TestCode `ControlFlow/RangeForReference.cf`, `Types/BracedAssignment.cf`; IsolatedTest
  `Functions/MainPrefixedName`; C4Testing `BracedInitializerTest`; NegativeTests `MissingReturn`,
  `MissingReturnInBranch`, `MissingReturnAfterLoop`, `MissingReturnInLambda`, `MissingReturnInMethod`,
  `RangeForRefOverByValueDeref`, `RangeForRefTypeMismatch`, `RangeForRefConstElements`, `BracedListWithoutType`.
- **Steps:**
  1. `StmtLowerer::LowerRangeFor` / `LowerRangeForIterator`: a `T&` / `const T&` loop variable binds a
     reference local (reuse `LowerLocalDecl`'s reference handling) to the element location each
     iteration; a by-value `dereference` with a reference loop variable is an error.
  2. New `Backend/HIR/Reachability.{h,cpp}`: "can this statement list complete normally" over HIR;
     report `control reaches the end of non-void function 'F'`. `FunctionEmitter` then terminates
     leftover blocks of non-void functions with `unreachable` instead of `ret null`.
- **Tests:** TestCode `ControlFlow/RangeForReference.cf`; NegativeTests `MissingReturn`,
  `MissingReturnInBranch`, `RangeForRefOverByValueDeref`.

#### B39 - SIMD: target features, 256- and 512-bit widths, intrinsics (roadmap 1a)
- **Closes:** BUG-250, BUG-251, BUG-252. **Design:** D-62, [vectors 7-9](../language/11-vectors-and-swizzling.md#8-the-simd-target),
  [builtins 11](../language/14-builtins.md#11-simd-intrinsics).
- **Depends on:** B20 step 6, B30 (done). **Status:** Done.
- **Done notes:** `Compiler/CpuTarget` reads the flags as clang does (the last `-march`, then every `-m` flag in
  order, implications through `llvm::X86::updateImpliedFeatures`, `native` through the host's CPU and features);
  `CompileOptions::target`, `CompilerInstance::SetTarget` / `ProgramModel::Target`, `PredefinedMacros` adds the
  `SIMD_*` macros, `Backend/CodeGen/TargetAttributes` puts `target-cpu` / `target-features` on every definition,
  `clang -c` gets `-march`, and the interop pass hands the flags to the foreign compile and to CBind (its binding
  cache keys on them: `BindingFlags`); CBind binds C vector types (`__m128` .. `__m512`, `vector_size`) as the
  `packed T[N]` of the same lanes. HIR: `SimdNode`, `Backend/HIR/SimdIntrinsics` (dispatch, lane math, masks,
  reductions), `SimdOperands` (reading arguments as lanes; a pure packed object becomes its lanes through a no-op
  conversion), `SimdLanes` (shuffle, concat, splat), `SimdMemory`; `Backend/CodeGen/SimdEmitter`. Broadcasts:
  `ImplicitConversion::Scalar` and a broadcast cost in `OverloadResolver::ParamCost` (BUG-250); casts:
  `CastRules` lane and bit rules, `ExprLowerer::VectorCast`, `ConvertNode::keepsBits`, a lane-aware
  `EmitConvert`. `ConstantOperands::IntegerOf` folds `+ - *` of constants (a shuffle index `P + 4` in a template).
  Runtime and Bedrock: `raw_alloc_aligned` / `raw_free_aligned`, `Container/storage.ch` (`AllocateElements`,
  `FreeElements`) under every container (BUG-251). Scripts: `.c4i` format 2 records the target, `HostIndex::target`,
  the JIT and the script compile use it (BUG-252). The LSP's `target` option; Nuke's editor bridge fills it from the
  project's `compilerArgs`. Bedrock Math: component-wise functions, `==`, `NearlyEqual` and `Dot` on intrinsics,
  `Mat4::Inverse` shuffled as GLM's SSE version (12.1 to 9.6 ms, now 1.03x GLM SSE), fused `Mat4::Transform`, and
  `Math/batch.{ch,cf}`. Benchmarks: `Benchmarks/math/per-vector/` (with `-March`) and `batch/` (C4 within 5
  percent of hand-written AVX2, 1.4 to 2.6x GLM's loops). Found and fixed on the way: BUG-253 (a bool converted to
  a float was -1), a reference to a packed vector of integer lanes lost its address in a conversion (the new
  lane-aware conversion unwrapped references; IsolatedTest `Simd/ReferencesToIntegerLanes`). Tests: C4Testing
  `Compiler.CpuTarget` (12), `Semantic.SimdIntrinsics` (11), `Script.HostInterface` (the target, an unknown one);
  IsolatedTest `Simd/*` (13 cases, 3 for other targets), `Types/Bug253BoolToFloat`, `Interop/SimdVectorArguments`,
  `Interop/SimdIntrinsicsInC` (AVX2 intrinsics in C); 32 NegativeTests (`Simd*`,
  `Packed*`); ScriptTest `WideVectors`, `WideVectorsAvx2` (and `flags.txt` for a case's host); ToolsTest
  `TargetFlags`; Bedrock `math_batch_tests.cf`, `container_alignment_tests.cf`, `math_vector` component tests.
- **Steps:**
  1. `Compiler/CpuTarget.{h,cpp}`: the target of a build from `-march` and `-m` / `-mno-` flags (`native` through
     `llvm::sys::getHostCPUName` / `getHostCPUFeatures`, implied features through `llvm::X86::updateImpliedFeatures`),
     its macros and its LLVM feature string; unknown CPUs and features are usage errors. `CompileOptions` holds it,
     `PredefinedMacros` adds the `SIMD_*` macros, `ProgramModel` carries it for lowering.
  2. Codegen: every defined function gets `target-cpu` / `target-features` (`Backend/CodeGen/TargetAttributes`);
     `clang -c` gets `-march`; foreign C/C++ sources get the build's flags.
  3. Values: a scalar broadcasts into a `packed T[N]` (BUG-250); lane-wise casts and `reinterpret_cast` between
     packed vectors (`Backend/HIR/CastRules`, `ExprEmitter::EmitConvert`).
  4. Intrinsics: `Backend/HIR/SimdIntrinsics.{h,cpp}` checks and lowers the `simd_*` calls to a `SimdNode`;
     `Backend/CodeGen/SimdEmitter.cpp` maps each to LLVM vector IR or intrinsics.
  5. Alignment (BUG-251): `raw_alloc_aligned` / `raw_free_aligned` in the runtime and prelude; Bedrock's
     containers allocate their buffers through `Core::AllocateElements<T>` / `FreeElements<T>`, aligned to
     `alignof(T)`.
  6. Scripts (BUG-252): the `.c4i` (format 2) records the host's CPU and features; a script compiles with the
     host's target (macros, intrinsic checks, function attributes) and is linked by a JIT for that target.
  7. Bedrock Math on the intrinsics where it pays (component-wise functions, comparisons, dot products,
     `Inverse` shuffles, fused products), and `Bedrock/Math/batch.{ch,cf}`: `TVector3Batch<T, N>`,
     `TVector4Batch<T, N>` (structure of arrays, N vectors per operation), their arithmetic, products and lengths,
     loads from and stores to arrays of vectors, and matrix transforms.
  8. `Benchmarks/math/`: batch kernels (8-wide transforms, normalizing, dot and cross products) against GLM and
     hand-written AVX2 C++; the runner takes `-March`.
  9. The LSP's `target` option (the same flags), which Nuke's editor bridge fills from the project's compiler
     arguments.
- **Tests:** C4Testing `Compiler.CpuTarget` (flags, implications, macros, errors), `Semantic.SimdIntrinsics`
  (lowering and IR per family, target attributes); IsolatedTest `Simd/*` per family (lane math, masks, shuffles,
  reductions, memory, casts, wide vectors and their alignment, fused multiply-add, target macros) at `-O0` and
  `-O2`; NegativeTests for every diagnostic (shapes, constants, ranges, element kinds, `simd_fma` without `fma`,
  casts); ScriptTest `WideVectors` (a `packed float[8]` both ways across an AVX2 host and its script); ToolsTest
  `TargetFlags`; Bedrock `math_batch_tests.cf` and the math tests; `Benchmarks/math/run-benchmark.ps1 -Check`.

#### B41 - Strict aliasing: type-based alias metadata (roadmap 5.2)
- **Closes:** BUG-254. **Design:** D-64, [types 4.14](../language/04-types.md#414-strict-aliasing). **Depends on:** B39
  (done). **Status:** Done.
- **Done notes:** `Backend/CodeGen/AliasClass` (the class of a type and of an access, with the union walk through
  `Field`, `Base`, lanes and indexed arrays: `IndexedArray` moved from `ReadOnly.cpp` to `IndexLValue`) and
  `AliasTags` (the nodes, owned by `ModuleContext`); `ExprEmitter`'s `MemoryAccess` is now `{ isVolatile, aliasTag }`
  from `AccessOf(loc, type)`, put on loads by `Load` and on stores by `GcStore`; `SimdEmitter` tags the `simd_load` /
  `simd_store` families and gathers. A named union's member was reached through a `Base` view with no type
  (`SubobjectLValue` with no owner), which hid the union; an ownerless member is now in its base. `-fno-strict-aliasing`:
  `CompileOptions::strictAliasing`, `CodeGenDriver::DisableStrictAliasing`. `CastRules::KeepsBits` marks a
  `reinterpret_cast` between numbers (and vectors); Bedrock's puns in `hash.ch`, `dom.ch`, `dom.cf` and `format.cf`
  use it. Found and fixed on the way: BUG-254 (the store of an object with vptrs dropped `volatile`). Benchmark: the
  new `batch_pointers` kernel runs as fast as `batch_transform` (0.41 ms; 0.46 ms with `-fno-strict-aliasing`) and
  ahead of the AVX2 version, which clang compiles with reloads (a `__m256` may alias anything to it).
- **Why:** a batch kernel reading arrays through pointers held in a struct ran 10-20 percent behind hand-written
  AVX2: with no alias information LLVM reloaded every pointer after each vector store.
- **Steps:**
  1. `Backend/CodeGen/AliasClass.{h,cpp}`: the class of a type (bytes, bool, 16-, 32-, 64-bit integers, float,
     double, pointer; an object, array, vector or union is its leaves' class when they share one, else bytes) and
     of an access (bytes when it goes through a union member of a different class).
  2. `Backend/CodeGen/AliasTags.{h,cpp}`: the `!tbaa` scalar type nodes and access tags; `ExprEmitter` tags every
     load and store of a location (whole objects, lanes and runs too), and `SimdEmitter` the `simd_load` /
     `simd_store` families. Stack slots the compiler fills, vptrs, `va_arg` and atomics stay untagged.
  3. `-fno-strict-aliasing`: `CompileOptions::strictAliasing`, `CodeGenDriver::DisableStrictAliasing`.
  4. `reinterpret_cast` between numbers of the same size keeps the bits (`CastRules`, `ConvertNode::keepsBits`);
     Bedrock's six pointer puns (`hash.ch`, `dom.ch`, `dom.cf`, `format.cf`) use it.
  5. `Benchmarks/math/batch`: a kernel that reads its arrays through pointers held in a struct, checked against
     the AVX2 version.
- **Tests:** C4Testing `Semantic.AliasTags` (9: the class of each type, objects, union members and union array
  elements, packed lanes, SIMD memory, struct-held pointers, the flag off, bit casts); IsolatedTest
  `Types/StrictAliasingUnions`, `StrictAliasingBytes`, `StrictAliasingBitCasts`, `StrictAliasingStructPointers` (the
  pointers come from volatile variables, so a wrong tag gives a stale value at `-O2`) and
  `Types/Bug254VolatileObjectWithVptrs`, with IR checks; NegativeTests `ReinterpretCastFloatToInt` (now a size
  mismatch), `ReinterpretCastBool`, `ReinterpretCastEnum`, `ReinterpretCastFloatToPointer`; ToolsTest
  `StrictAliasingFlag`; `Benchmarks/math/run-benchmark.ps1 -Check` (six batch kernels).

### Testing framework

#### B16 - Interop binding fidelity
- **Closes:** BUG-086, BUG-087, BUG-088. **Design:** [interop](../language/13-interop.md).
- **Depends on:** none. **Status:** Done.
- **Done notes:** CBind binds overloads (and creates) under their C++ name with the overload's index in the
  wrapper symbol only (`OverloadSymbol`, also fixing same-arity overloads that collided); static data members
  as `[link("<decorated>")] public static T` (`clang::ASTNameGenerator`), which the model now treats as the
  linked variable (`Model/Builder/LinkSymbol` is the one `[link]` reader for functions, globals and statics).
  Line numbers: the interop pass appends the bindings after the user's text instead of prepending them,
  so nothing moves; the LSP's line shift (`ToEffectivePosition`) is gone and a location inside the bindings
  maps to its directive. Found and fixed on the way: BUG-149 (`unsigned int` was `int32`, `unsigned char`
  `char8`, `wchar_t` `uint16`) and BUG-150 (const methods bound as mutable, private fields as public, pointees
  lost their const). Tests: IsolatedTest `Interop/CPlusPlusInterop` (overloads by name, static data read and
  written from both sides, a const method on a const object, unsigned/char/wide types); NegativeTests
  `InteropErrorLine`, `CppPrivateField`, `CppMutableMethodOnConst`, `CppConstPointerWrite`.
- **Steps:**
  1. `CBind/src/BindingGenerator.cpp`: bind an overloaded C++ method under its own name (C4 overloads by
     parameter types); keep the `_N` suffix only in the generated wrapper's `[link]` symbol. Change
     `Interop/CPlusPlusInterop` to call `b->Deposit(100)` / `b->Deposit(50, 5)`.
  2. Static data members: CBind emits `[link("<MSVC decorated name>")] public static T name;` in the bound
     type (the name from `clang::MangleContext`). Model: a static data member with `[link]` is an
     external global named by the link symbol (the same path as a linked free global,
     `Interop/LinkVariable`), so `Counter::g_global` reads and writes the C++ variable. Fill
     `TestStaticData` in `Interop/CPlusPlusInterop` (read 100, `Counter_IncrementGlobal()`, read 101, write).
  3. Line numbers: `Interop/InteropPass.cpp` registers each header's bindings as their own in-memory source
     (the `.c4bind` name) and `ApplyEffectiveSourceTexts` (`src/Source.cpp`) replaces each
     `extern "c++" include "x.h"` line with `include "<binding source>"` on the same line and each
     `extern "c++" source` line with an empty line, so the user's lines do not move.
- **Tests:** IsolatedTest `Interop/CPlusPlusInterop` (overloads, static data); NegativeTest
  `InteropErrorLine` (a type error on line 5 after an `extern "c++" include`, expect `main.cf (5:`).

#### B17 - Rethrow re-test
- **Closes:** BUG-089. **Depends on:** none. **Status:** Done.
- **Done notes:** with `Lifecycle::GuardWith` switched to `try { } catch (...) { undo; throw; }`,
  `Lifecycle/ConstructionExceptionSafety` passed at `-O0` and `-O2`, and so did every IsolatedTest at `-O2`
  and TestCode: the bug no longer reproduces, so BUG-089 is deleted. The shipped guard stays the
  exception-only cleanup. Regression test: IsolatedTest `Exceptions/CatchAllRethrowGuards` (catch-alls that
  undo and rethrow after an earlier rethrow, inside a function and around a partly built array).
- **Steps:**
  1. Follow the repro in BUG-089 on a scratch branch: make `Lifecycle::GuardWith` emit
     `try { } catch (...) { undo; throw; }`, rebuild, run `Lifecycle/ConstructionExceptionSafety` at `-O2`.
  2. If it fails: dump the optimized IR (`clang -O2 -S -emit-llvm out.ll`) and the unwind tables
     (`llvm-objdump --unwind-info`), write the same shape in C++ and compare clang's output; fix the
     difference in `RegionEmitter` (rethrow inside a catch funclet) and add the C4 repro as an IsolatedTest
     run at `-O2`. If clang's C++ fails the same way, report it upstream and keep the cleanup-based guard.
  3. If it passes: delete BUG-089.
- **Tests:** the IsolatedTest from step 2, at `-O0` and `-O2`.

#### B18 - Test framework fixes
- **Closes:** BUG-021, BUG-022, BUG-038. **Design:** D-45, [testing](../runtime/03-testing.md).
- **Depends on:** B4 (`TEST`, done). **Status:** Done.
- **Done notes:** `Model/TestRegistry.{h,cpp}` replaces the model's ad-hoc registry: it collects where each
  function's record exists (so a test binds its record, not a name prefix that `TestAdd_Helper` could also
  match), composes a class's `[test("a.b")]` with its methods' groups (a bare method test is
  `a.b.<Function>`, not just `a.b`), and rejects non-static test methods and fixtures, parameters, non-void
  returns and a second setup or teardown. `Backend/CodeGen/TestHarnessEmitter.{h,cpp}` is the harness:
  `main(argc, argv)`, `c4_test_configure` for `--filter`, `c4_test_selected` per case, the test function's
  file:line to `c4_test_begin`, `c4_test_now_ns` timing, and `-test-json` passed to `c4_test_summary`
  (`TestHarnessOptions`). The runtime records each case and writes valid, escaped JSON with a `cases`
  array. The IsolatedTest runner gained `compileFlags`, `runArgs` and `jsonFile` in `ExpectedExit.json`.
  Tests: IsolatedTests `TestingFramework/ClassGroupPrefix`, `FailureFileLine`, `JsonSummary`, `GroupFilter`;
  C4Testing `ClassTestGroupPrefixesItsMethods` and the updated registry tests; NegativeTests
  `TestSetupMustBeStatic` (now the specific error), `TestMethodNotStatic`, `TestWithParameters`,
  `TestReturnsValue`, `FixtureWithParameters`, `SecondGlobalSetup`.
- **Steps:**
  1. `ProgramModel::CollectTestDecl`: a class's `[test("a.b")]` prefixes its methods' groups
     (`a.b.<method group>`; an empty method group gives `a.b`). Non-static `[test]`, `[test_setup]`,
     `[test_teardown]` methods are errors (`test methods must be static`); fixtures take no parameters
     and return void.
  2. Extract the harness from `CodeGenDriver.cpp` into `Backend/CodeGen/TestHarnessEmitter.{h,cpp}`;
     pass the source file and line to `c4_test_begin`, time each case (runtime helper `c4_test_now_ns`),
     print the group path instead of the symbol.
  3. `-test-json <path>`: the harness calls `c4_test_summary(path)`; make `writeJsonSummary`
     (`C4Runtime/TestRuntimeC.c`) emit valid JSON (escaped strings, a `cases` array).
  4. `--filter <glob>` on the test executable (`*` and `?` on group paths); fixtures still bracket.
  5. Update `NegativeTest/TestSetupMustBeStatic`'s expected message.
- **Tests:** IsolatedTests `TestingFramework/ClassGroupPrefix`, `TestingFramework/FailureFileLine`
  (output contains `file.cf:12`), `TestingFramework/JsonSummary` (the runner validates the file with
  `ConvertFrom-Json`), `TestingFramework/GroupFilter`.

#### B37 - Assertion locations, exceptions inside tests
- **Design:** [testing](../runtime/03-testing.md). **Depends on:** B18 (done). **Status:** Done.
- **Done notes:** the call is rewritten to a `c4_assert_*_at` form with the file and line appended
  (`Backend/HIR/AssertionSites.cpp`), so an assertion inside another's arguments keeps its own line;
  `ProgramModel::SourceName` carries the display names. Cases run through `c4_test_run_case`
  (`C4Runtime/TestCaseRunner.cpp`). The unused `assert_*_info` runtime functions were removed.
- **Steps:**
  1. HIR: a call of an assertion builtin (`assert_true`, `assert_eq`, `assert_feq`, `assert_se` and the `_info`
     forms) first calls a runtime `c4_test_at(file, line)` with the call site's file and line (the unit's
     display name reaches lowering the way `TestHarnessOptions::fileOf` reaches the harness); `reportFail`
     prints that location instead of the test's.
  2. `TestHarnessEmitter::RunCase` wraps the call in a catch-all: an exception that leaves a test fails that
     case (`FAIL: <test>: an exception left the test`) and the run continues with the next case.
- **Tests:** IsolatedTests `TestingFramework/AssertionLine` (two failing asserts on different lines, both
  lines printed) and `TestingFramework/ThrowingTest` (a throwing case fails, the next case still runs; exit
  code counts it).

### Runtime, reflection, library

#### B19 - Reflection rework
- **Closes:** BUG-036, BUG-037. **Design:** D-42, D-50, [reflection](../runtime/02-reflection.md).
- **Depends on:** A2, A4 (done). **Status:** Done.
- **Done notes:** identity, capacity and flags in `Backend/Reflection/RegistrationSource.cpp` and a rewritten
  `C4Runtime/ReflectRuntimeC.c` (B19a). Invoke thunks: `Backend/HIR/InvokeThunk.{h,cpp}` lowers one per
  function with a body or virtual method, `CodeGenDriver::EmitInvokeThunks` emits them and hands
  `InvokeThunks` to the registration; `Field.Get/Set` (B19b). Every function now has its own symbol
  (`FunctionSymbol`, `MethodSymbol`, `SymbolToken`, and `SymbolTable::AddFunctionRecord` keeps them unique).
  Annotations (B19c): the parser takes expression arguments (`AnnotationNode::ParseArguments`, string
  literals for the compiler's own); `FunctionRecord::declarations` lists every declaration and definition,
  object and enum types keep their declaration node; `Model/Annotations.{h,cpp}` folds them and the
  registration reports a failure as a compile error. ABI: annotation tables on every entity, field and
  parameter; `c4_reflect_on_unload` / `c4_reflect_remove_listener`. Bugs found and fixed on the way:
  BUG-151 to BUG-160. Tests: Bedrock `reflection_tests`, `reflection_invoke_tests`,
  `reflection_annotation_tests`; IsolatedTests `Reflection/ManyTypes`, `DistinctSymbols`,
  `UnloadAndReregister`; C4Testing `Semantic.DeclarationNodes`, `Semantic.ReflectedAnnotations`,
  `Parser.Annotation`; NegativeTests for the three annotation errors; TestCode `QualifiedMembers`,
  `LocalEnumMembers`, `ThisArgument`.
- **Steps:**
  1. `ProgramModel::EmitReflectRegistrationC`: register types and functions under the fully qualified
     display name (`Ns::Type`, `Box<int32>`, `Geo::Pair<float, int32>`); emit parameter names.
  2. `C4Runtime/ReflectRuntimeC.c`: growable arrays plus an open-addressing hash index by name.
  3. Invoke (done): codegen emits `void __c4_invoke_<sym>(void* object, void* result, void** arguments)`
     for every function reflection registers that has a body or is virtual; the table stores its address.
     Bedrock `Reflection::Function` / `Method` gain `Invoke` plus typed `Call` helpers, `Field` gains
     `Get` / `Set`.
  4. Accessors for stored flags (`isConst`, `isStatic`), a per-type list of static members, unknown
     annotations as `(name, args[])` records (`Reflection::Annotation`).
  5. Reload callbacks: `c4_reflect_on_unload(tag, fn, user)`.
- **Tests:** Bedrock `reflection_tests.cf` (instance and namespaced names, invoke free / static /
  instance / virtual, parameter names, annotations, more than 1024 types from a generated source);
  IsolatedTest `Reflection/UnloadAndReregister`.

#### B20 - Runtime and Bedrock fixes and additions
- **Closes:** BUG-039. **Design:** D-46, D-53, D-58, [Bedrock](../library/bedrock.md).
- **Depends on:** steps 1-2 none; steps 3-6 need A3 and A4 (done). **Status:** Done.
- **Done notes (B20a):** 64-bit file positions and `file_seek_from`; `memmove`, `format_double`,
  `parse_double` (`C4Runtime/TextRuntimeC.c`) and `int64` byte helpers. Bedrock reorganized by D-58:
  `Core::Array` (was `Vector`), `Core::HashMap` with `KeyTraits` / `ByteKeyTraits`, `TryGet` and `Find`,
  `Core::Queue` / `Core::Stack` with range-for, `Core::Sort` (introsort) and binary searches,
  `Core::StringView` (was `Str`), `Core::String`, `Core::StringBuilder` (exact float formatting),
  `Core::Format` (any order, repeats, brace escapes, `s`), `FS::File` (`IO/` became `FS/`),
  `Serialization::` DOM (lookups no longer add nodes), `WriteJson`, `WriteYaml` and `WriteBinary` /
  `ReadBinary` (bounds-checked). Compiler bugs found on the way and fixed: pointer difference (BUG-168),
  `Ns::Template<Args>(...)` construction (BUG-169), `this` in a comparison (BUG-170), `*this` beside a
  dereference operator (BUG-171), static members through a template parameter (BUG-172), float `!=` with
  NaN (BUG-173). Bedrock and runtime bugs: BUG-039, BUG-162 to BUG-167, BUG-174, BUG-175.
- **Done notes (B20b, B20c):** `Core::List`, `Core::InitList`, `FS::Path`, `FS::Directory` and the tree functions
  over new `fs_*` runtime calls (`C4Runtime/FileSystemRuntimeC.c`), `Serialization::ReadJson` and
  `Serialization::ReadYaml` with `ReadResult` positions. Bugs fixed on the way: UTF-8 paths on Windows
  (BUG-176), nested braces in an `__init_list` (BUG-177), template names resolved from the wrong scope
  (BUG-178), types nested in a class template (BUG-179), the shortest float text (BUG-180).
- **Done notes (step 6):** `Bedrock/Math/` and `math.ch`: scalar functions, `TVector2/3/4<T>` over packed lanes,
  `Mat2/3/4`, `Quat`, transforms and cameras, with one YXZ Euler convention across matrices and quaternions
  (GLM's `quat(vec3)` order differs; documented in Bedrock section 9). Tests `Bedrock/Tests/math_*_tests.cf`;
  `Benchmarks/math/` against vendored GLM 0.9.9 (plain and SSE). Bugs fixed on the way: a namespace global of a
  namespace type (BUG-186), alias-typed fields with no size (BUG-187), a packed vector's default create never
  running (BUG-188), ignored delete and copy on a packed vector (BUG-189), folded constants loaded from memory
  (BUG-190).
- **Steps:**
  1. `C4Runtime/IORuntimeC.c`: `_fseeki64` / `_ftelli64`; add `file_seek_from(h, offset, whence)`.
  2. `Bedrock/Container/hashmap.cf`: add `bool TryGet(const K& key, V& out)` (copies the value into `out`
     through `Lifecycle` assignment, D-06).
  3. `HashMap<K, V>` hashes keys through `operator_hash` / `operator_equal` when `K` declares them
     (on-demand members from A4 make this a plain member call); keep the policy overloads.
  4. `Core::List<T>`, directory listing (`Bedrock/FS/directory.{ch,cf}` over new runtime calls), JSON and
     YAML readers into the existing DOM.
  5. `template<T> alias InitList = __init_list<T>;`.
  6. Math library (D-46) in `Bedrock/Math/`, `namespace Math`: `Vec2/3/4<T>` with packed views and swizzles, `Mat3/4`,
     `Quat`, dot, cross, length, normalize, lerp, look-at LH, perspective LH 0-1 NDC, ortho, inverse,
     transpose, slerp, axis-angle, quat <-> matrix; column-major like GLM. Benchmarks against GLM in
     `Benchmarks/math/`.
- **Tests:** Bedrock tests per type and function; `Benchmarks/math/run-benchmark.ps1`.

#### B40 - Platform library
- **Design:** D-58, D-63, [Bedrock](../library/bedrock.md#12-platform-platform). **Depends on:** B20, B29 (done).
  **Status:** Done.
- **Done notes:** steps 1-3 as planned. `C4Runtime/PlatformRuntimeC.h` and `C4Runtime/Platform/{VirtualMemory,System,
  Library,Time}Win32.c`; `c4_wide_to_utf8` joined `c4_utf8_to_wide` in `PathRuntimeC.h`. Bedrock `Platform/` with 38
  tests (`platform_{memory,system,library,time}_tests.cf`: refusals, round trips, recommitted pages zeroed, rights seen
  through `Query`, aligned reservations, code written, protected and called, memory totals that move, environment
  variables unset, empty, long and UTF-8, libraries by name and path, missing ones and files that are none, calendar
  fields to the ends of an int64) and IsolatedTest `Platform/ProtectionFaults`. Found and fixed on the way: a
  namespace's function and `&F` were no values (BUG-263, IsolatedTest `Calls/QualifiedFunctionAddress`), a function
  template's instance `F<int32>` was no value (BUG-264, IsolatedTest `Templates/FunctionInstanceValue`, NegativeTest
  `TypeInstanceAsValue`), and a git worktree could not build: Frost's link command outgrew Windows' limit (BUG-260),
  `.gitignore` hid C4DAP's OutputPipes sources (BUG-261), Frost ignored a project's own name (BUG-262) and plain `-b`
  built early projects out of dependency order (BUG-265); ToolsTest `FrostLongLink`, `NoIgnoredSources`,
  `FrostProjectName`, `FrostBuildOrder`.
- **Steps:**
  1. Runtime C API, `C4Runtime/PlatformRuntimeC.h`: `c4_vm_*` (page size, allocation granularity, reserve with an
     alignment, allocate, commit, decommit, release, protect, query, flush the instruction cache), `c4_system_*`
     (processor and cache counts, the machine's and the process's memory, the process id, the executable's path,
     getting, setting and removing environment variables), `c4_library_*` (open with an error text, find a symbol,
     close) and `c4_time_*` (the counter and its frequency, Unix nanoseconds, the local UTC offset at an instant).
     Windows implementations in `C4Runtime/Platform/{VirtualMemory,System,Library,Time}Win32.c`; the UTF-16 helpers
     the file system already has are shared through `PathRuntimeC.h`.
  2. Bedrock `Bedrock/Platform/{memory,system,library,time}.{ch,cf}` in `namespace Platform`, included by
     `bedrock.ch`: the `[link]` declarations and wrappers that turn codes into enums, buffers into `Core::String` /
     `FS::Path`, and Unix time into calendar fields (days-from-civil in C4).
  3. Docs: Bedrock section 12, the roadmap's item 3, the implementation status, the test-suite counts.
- **Tests:** Bedrock `platform_memory_tests.cf` (zero and negative sizes, reserve / commit / decommit / release
  round trips, zeroed pages after a recommit, protection changes seen through `Query`, alignment, release of an
  interior address, executable code written, protected and called), `platform_system_tests.cf` (counts, memory
  totals that move when memory is committed and touched, the executable's path, environment variables: unset,
  empty, UTF-8, removed, invalid names), `platform_library_tests.cf` (a system DLL opened and a function called
  through `Find`, missing libraries and symbols, closed and empty libraries, two handles to one module),
  `platform_time_tests.cf` (calendar fields at the epoch, before it, leap days, the limits of int64 nanoseconds,
  round trips, the local offset, the counter's monotony). IsolatedTest `Platform/ProtectionFaults` (built with
  `--script-host` for `Scripting::Guard`): writes to a read-only page, reads of no-access, reserved, decommitted
  and released pages, and execution of a page without execute rights each fault with the touched address.

### Tools

#### B21 - Compiler CLI and toolchain layout
- **Closes:** BUG-042..BUG-048. **Design:** D-56, [CLI](../tools/01-compiler-cli.md).
- **Depends on:** none. **Status:** Done.
- **Done notes:** an option table drives `ParseArgs` and `PrintUsage`; usage errors exit 2. New
  `Compiler/Toolchain.{h,cpp}` finds the home (`C4_HOME`, else the executable's folder), clang, the runtime,
  CBind and Bedrock, with the repository fallback only for a development build; a missing piece is an error
  that names the folder. The runtime folder kept its name `C4Runtime/` in the install. `Nuke/Setup.py`
  `install_c4` copies an install (also used by the kit) and `install_problems` checks one; the kit's source
  mirror no longer wipes the staged `Nuke/c4/`. Nuke passes `--int-dir` and each project's (and linked
  module's) `links` as `-l`. A missing `Bedrock/` is an error instead of an empty library. Found: BUG-198
  (the LSP does not load Bedrock; B27).
- **Steps:**
  1. `Compiler/BuildUtils.cpp` `ParseArgs`: `-I <dir>` keeps its meaning - the folder is a project the build
     depends on: its headers resolve and its sources compile with the rest (the owner's decision, which
     BUG-041 had mistaken for a bug) - but it consumes its argument explicitly instead of falling through as an
     input. Per-project settings are B38. Unknown flags are errors. No inputs is a usage error (remove the
     `assets/TestCode` default and the crash handler from `src/Source.cpp`; fix its header comment).
  2. Real `-c` (stop after the object), `-bc` (write bitcode), `-gc-stats` (the `@main` wrapper calls
     `c4_gc_install_default_stats_printer()` first), `-test-json` (B18), `-D` (B4), `--int-dir <dir>`
     (intermediates go there), `-l <lib>` and `-L <dir>`.
  3. `FindLLVMBinDir`, `EnsureRuntimeCObject` and the CBind lookup (`Interop/InteropPass.cpp`) resolve
     relative to `C4.exe` (`toolchain/`, `C4Runtime/`, `CBind/`) with a `C4_HOME` override, falling back to
     the repo layout only in a development build. (Runtime objects already compile at `-O2`: A5.)
  4. Root `Build.py` copies `C4Runtime/` to `bin/.../C4/C4Runtime/` and the vendored clang/lld to
     `toolchain/`; `Frost/Builder/NukeKit.py` copies the same tree plus CBind.
  5. `Nuke/ProjectInit.py` writes a `GetProject()`; `ProjectManager.Reload` reads `loadedProj` only
     inside the `hasattr` check.
  6. `PrintUsage` lists every flag.
- **Tests:** a new suite `assets/ToolsTest/RunTests.ps1`: copy the kit to a temp folder outside the
  repo, build and run a hello world and an `extern "c"` program, `nuke init` + `nuke build` there, and
  check each flag's effect. Add it to the gates.

#### B22 - Diagnostics
- **Closes:** BUG-049. **Design:** D-32, D-42.
- **Depends on:** none. **Status:** Done.
- **Done notes:** warnings are printed for a build that succeeds (they were dropped); `-Werror`, `-w`;
  `ImplicitConversion::WarnIfNarrowing` at initializations, assignments, arguments and returns (constants that
  fit, folded through `ConstEvaluator`, stay quiet); `AnnotationItem::NearCompilerAnnotation`. Array sizes and
  lane counts report at the extent why they do not fold (a variable, a startup const, a form that does not
  fold), and `?:` folds in constants. The narrowing warning found tests that compared floats through `int32`
  asserts (`FloatLiterals`, `overload_tests`): fixed to compare what they meant.
- **Steps:**
  1. A `Warning` severity end to end (`ErrorTracker`, `DiagnosticReporter`, LSP mapping), with
     `-Werror` and `-w`.
  2. Warnings: implicit narrowing (to a narrower int, float -> int, double -> float) unless the value is
     a constant that fits; an unknown annotation within edit distance 2 of a compiler annotation.
  3. `Model/ConstEvaluator.cpp`, `Model/TypeResolver.cpp`: an array extent that does not fold reports
     `array size is not a constant expression` at the extent with the evaluator's reason; a const used
     before it can fold reports the cycle or the missing name. Update the four NegativeTests.
- **Tests:** NegativeTests per message; IsolatedTests `Diagnostics/WarningsDoNotFail`,
  `Diagnostics/WerrorFails`.

#### B23 - Prelude and `_GUID`
- **Design:** D-43, [builtins](../language/14-builtins.md).
- **Depends on:** B21, A5, B12 (all done). **Status:** Done.
- **Done notes:** `Bedrock/Prelude/prelude.ch` (186 `[link]` functions and `_GUID`) replaces
  `Model/BuiltinDeclarations.cpp`. `SourceManager::RegisterIncludedLibraryFiles` always registers it
  (`PreludeId()`); `ModelBuilder::VisitPrelude` visits it last and skips a name the program declared at file scope
  (functions and types); every unit's include closure gets it; its free functions are `isBuiltin`. `__uuidof(T)`
  without `[uuid]` is `uuid5(NAMESPACE_URL, "c4:" + QualifiedName(T))` (`Model/Types/NameGuid`), and `__uuidof` of
  a name that is not a type reports the resolver's reason. Tests: C4Testing `Semantic.Prelude` (6), IsolatedTest
  `Interop/UuidofComputed`, `Types/UuidOfNegative` now an unknown type.
- **Steps:**
  1. `Bedrock/Prelude/prelude.ch`: every builtin of `Model/BuiltinDeclarations.cpp` as a C4 declaration
     with `[link("c4_...")]` (CRT functions keep their names), plus `struct _GUID`.
  2. `CompilerInstance`: every unit implicitly includes the prelude, so builtins stay visible without an
     include. Delete the C++ declarations.
  3. `__uuidof(T)` without `[uuid]`: a deterministic v5 UUID of `"c4:" + qualified name`.
- **Tests:** every suite (no behavior change); IsolatedTest `Interop/UuidofComputed`.

#### B24 - Order-independent constants in every position
- **Design:** [program model 1.7](../language/01-program-model.md#17-order-independence-and-constants).
- **Depends on:** B3, B22. **Status:** Done: the folding from B3 already reached every position; verified by
  TestCode `Core/ForwardConstUse` (signatures, defaults, enum values, template arguments, aliases, statics,
  case labels, lane counts, namespaced chains across files).
- **Steps:** fold on demand from any expression position during pass 2 (array extents in signatures,
  default arguments, enum values from another file); turn the NegativeTests that should compile into
  TestCode cases.
- **Tests:** TestCode `Core/ForwardConstUse.cf`.

#### B25 - `namespace A::B { }`
- **Design:** D-51. **Depends on:** none. **Status:** Done (`NamespaceNode::ParseNestedNames`).
- **Steps:** `Parser/Nodes/Declarations/NamespaceNode.cpp` accepts a qualified name and nests.
- **Tests:** TestCode `Core/NestedNamespaceShorthand.cf`.

#### B34 - C++-style cast rules and reference `as`
- **Design:** D-11, D-12, [expressions 5 and 8](../language/10-expressions.md#7-type-tests-is-and-as).
- **Depends on:** A2; step 2 on B23 (`BadCast`). **Status:** Done.
- **Done notes:** step 1: `Backend/HIR/CastRules.cpp`, plus casts to a reference for every cast keyword (they yielded a
  temporary before). Step 2: `Backend/HIR/ExprCasts.cpp` (the reference and checked casts moved out of
  ExprLowerer.cpp) lowers `x as T&` / `dynamic_cast<T&>(x)` to the pointer cast of `&x` held in a temporary and
  `temp != null ? temp : __c4_bad_cast("T")`; the prelude declares `class BadCast { const char* target; }` and
  `__c4_bad_cast`, which throws it. Writing that test found BUG-201: a read-modify-write (`x OP= v`, `++x`, `x++`)
  evaluated its target twice (three times for `x++`); `Backend/HIR/EvaluatedOnce` now holds the target's pointer,
  index and receiver in temporaries. Tests: IsolatedTest `OOP/ReferenceAsThrowsBadCast`, NegativeTests
  `CheckedReferenceCastDropsConst`, `CheckedReferenceCastTemporary`, TestCode `Operators/EvaluatedOnce`.
- **Steps:**
  1. `ExprLowerer` (the cast keywords are one AST node kind with the keyword recorded): `static_cast`
     allows numeric conversions and pointers between related objects (`ClassHierarchy::DerivesFrom` either
     way, no run-time check); `reinterpret_cast` allows pointer <-> pointer and pointer <-> integer only;
     `const_cast` allows only adding or removing `const` (compare with `LoweringContext::DropsConst`);
     `dynamic_cast` stays `ResolveDynamicCast`. Each other combination is an error naming the keyword
     (`static_cast cannot convert 'A*' to 'B*': the types are unrelated`).
  2. `x as T&` / `dynamic_cast<T&>(x)`: take the address of `x`, run the pointer `DynamicCastNode`, and
     when it yields null throw `BadCast` (a struct declared in the Bedrock prelude, B23, or a builtin type
     until then). The result is a `Deref` lvalue of the cast pointer.
- **Tests:** NegativeTests `StaticCastUnrelated`, `ReinterpretCastFloatToInt` (since D-64: a `float` to an `int64`), `ConstCastChangesType`;
  IsolatedTest `OOP/ReferenceAsThrowsBadCast`, `Types/CppStyleCasts`.

#### B35 - Qualified call on an explicit receiver
- **Design:** C++ `p->Base::F()` / `obj.Base::F()`. **Depends on:** A2. **Status:** Done (`MemberAccessNode`
  qualifier, `ExprLowerer::QualifiedSubobject`; fields too: `obj.Base::x`).
- **Steps:** the postfix parser accepts a qualified name after `.` / `->`; `ExprLowerer::ResolveCall`
  resolves the method in the named type (which must be the receiver's type or one of its bases) and binds
  it with `BindReceiver(..., CallDispatch::Direct)`.
- **Tests:** IsolatedTest `OOP/QualifiedCallOnReceiver` (calls the base implementation of an overridden
  virtual through a pointer and through an interface sub-object); NegativeTest `QualifiedCallNotABase`.

#### B36 - Braced arguments for object and array parameters
- **Design:** D-57. **Depends on:** none. **Status:** Done.
- **Done notes:** a braced argument's type is a `BracedListType` of its entry count while the call resolves
  (`OverloadResolver::BracedCost`); `ExprLowerer::MaterializeBracedArguments` builds the chosen parameter's value,
  through function pointers too. A type with an `__init_list<T>` create takes braces of any count. Found on the
  way: positional braces filled statics, every union view and packed storage (BUG-195..197); every brace fill
  now uses `AggregateSlots`.
- **Steps:**
  1. `OverloadResolver::ParamCost`: a typeless (braced) argument also matches an object parameter whose
     aggregate fields or creates fit the entry count, and an array parameter of at least that many
     elements, at cost 3. The entry count has to reach `ParamCost` (today only the absence of a type does).
  2. `ExprLowerer::ResolveCall`: materialize a braced argument of an object or array parameter with
     `ResolveBracedAs(brace, param)`, next to the existing `__init_list<T>` case.
  3. The "no matching overload" error for a call with a braced argument says which parameters braces bind.
- **Tests:** TestCode `Functions/BracedArguments.cf` (by value, `const T&`, an array parameter, designators,
  an overload of `__init_list<T>` and an object preferring the list); NegativeTests `BracedArgumentTooMany`,
  `BracedArgumentAmbiguous`.

### Code health and long-term

#### B26 - Code health
- **Depends on:** the architect pass (done). **Status:** Done. Reviving the corpus found and fixed BUG-207 to
  BUG-227; its cases are IsolatedTest and NegativeTest folders whose reasons name the original test.
- **Steps:**
  1. Split files over ~600 lines by concern (re-check with `wc -l`): pure moves, no behavior change,
     delete orphan objects. The largest today: `Backend/HIR/ExprLowerer.cpp` (~2200 lines: split into
     calls, member access, operators, casts and initializers), `Model/Builder/ModelBuilder.cpp`,
     `CBind/src/BindingGenerator.cpp`.
  2. Split `CodeGenDriver::Lower` into one function per assembly step.
  3. `C4Testing/disabled_tests/`: revive what still has value, delete the rest.
  4. Empty `assets/Probes/`; make TestCode's `-O2` build write under `assets/build-output/testcode/`.
  5. Fix the stale documents in [implementation-status.md section 4](implementation-status.md).

#### B27 - LSP library split and tests
- **Depends on:** none. **Status:** Done.
- **Done notes:** `C4LSPLib/` (static library: Server, Protocol, Json, Features) and `C4LSP/` (`main.cpp`); the 12
  test files run in C4Testing (`Tests/LSP/`), plus `LspDefaultLibraryTest`. Fixed what they found: BUG-198 (the
  session registers the default library from `c4Home` or `Toolchain::Bedrock()`, which falls back to the
  repository for a development build), BUG-200 (`.` on a pointer and `->` on an object are errors again, as the
  owner made them in 1db84bf; the arrow quick fix is live), an argument mismatch against the only candidate names
  the argument and the parameter at the argument (`argument 1 of F has type 'X' but parameter 'p' expects 'Y'`,
  `F takes 2 arguments, not 1`), `const T&` prints its `const`, and `include not found` names the file.
- **Steps:** split `C4LSP` into a static library and a thin exe; move the 12 LSP tests from
  `C4Testing/disabled_tests/` to `C4Testing/src/Tests/LSP/` and make them pass.

#### B28 - Debug info and C4DAP
- **Depends on:** A1..A3 (done). **Status:** Done.
- **Steps:** (1) `DIBuilder` in codegen (compile unit per file, subprograms, locals from slots, line
  locations from the HIR) emitted as CodeView and linked with `/DEBUG`; (2) C4DAP as a DAP server over the
  platform debugger (write its design in `docs/tools/03-editor-lsp-dap.md` first).
- **Step 1 done:** `-g` (`Backend/CodeGen/DebugInfo`). Every HIR node carries `origin`, the statement it was
  lowered for (`HIRFunction::location`, set per statement by `StmtLowerer`), and the region emitter puts it on
  the builder, so every call and instruction has a line. A written function gets a subprogram named as the
  program spells it; `this`, the parameters and the program's locals (`Slot::declared`; compiler temporaries
  are `__...`) are declared at their slots; objects, unions, enums, arrays, swizzles, aliases, funcptrs and
  funcs are described; globals and static members by their scoped names, once. The link writes the `.pdb`
  (`/DEBUG`); Nuke passes `-g` for Debug and Development. Tests: C4Testing `Semantic.DebugInfo` (12),
  ToolsTest `DebugInfoFlag` (a real `.pdb` read with `llvm-pdbutil`), IsolatedTest `-DebugInfo`, and TestCode
  and Bedrock built with `-g`.
- **Step 2 done:** C4DAP (design and validation in `docs/tools/03-editor-lsp-dap.md` section 3). The JSON layer
  moved into `C4Protocol`, shared with the language server (and fixed BUG-202 on the way). The adapter is its
  own debugger over the Win32 debug API and DbgHelp (the system `dbgeng.dll` will not load its symbol engine
  outside the Debugging Tools): one engine thread owns the program; `int3` patches with the other threads
  suspended while one steps over a patch; source-line steps decided from the thread's registers alone (a call
  is the pushed return address, a return is the popped one, a recursive call's return breakpoint waits for its
  stack pointer); values shown by their CodeView types with C4 names; uncaught exceptions at the second chance;
  the program's stdout and stderr on pipes only it inherits; a step a throw leaves ends in the catch that takes
  it (stops on the handlers read from the MSVC EH tables). Along the way: BUG-203 (a catch handler's entry
  carried the try body's last line), `char8` is CodeView's narrow
  character, the runtime reports uncaught exceptions through `c4_report_uncaught_exception`, Frost removes
  stale objects before linking and its JSON parser reports malformed text instead of asserting, and Frost's
  unit tests can take a longer timeout (`TEST_FUNC_TIMEOUT`). Tests: C4Testing `Dap.*` (48), ToolsTest
  `DapAdapter`, `LspStdio`, `FrostStaleObjects`.

#### B29 - Threading
- **Design:** D-48, D-61. **Depends on:** A5 (done). **Status:** Done.
- **Done notes:** the atomic intrinsics (D-60, `docs/language/14-builtins.md` section 10). The collector's thread
  registry with suspend-and-scan, a re-entrant lock and a private heap for its own memory, per-thread allocation
  caches with a lock-free fast path, and thread_local roots (`__c4_gc_thread_roots`); runtime threads and waiting
  on an address (`C4Runtime/ThreadRuntimeC.c`); a reader-writer locked reflection registry (split into
  `C4Runtime/Reflect/`). thread_local storage holds any type: each thread starts its own copies (a
  `__c4_thread_start` per program that every attaching thread runs, a per-thread guard for locals) and destroys
  them as it ends (`C4Runtime/ThreadStorageRuntimeC.c`), which lifts B7's restriction. Found and fixed BUG-229 (a static
  local starting from a converted constant) and BUG-230 (a global's constant start was neither converted nor
  checked). Tests: IsolatedTest `MemoryGC/ThreadedCollection`, `MemoryGC/ThreadLocalRoots`,
  `Threads/StartJoinAndWait`, `Reflection/ConcurrentLookupsAndReload`, `Types/ConstantGlobalStarts`,
  `Types/StaticLocalConvertedStart`, `Diagnostics/GlobalStartWarnings`, `Lifecycle/ThreadLocal*` (objects, order at
  exit, retried starts, re-entry, a throwing delete, a foreign thread). The atomics take run-time orders too
  (the emitter switches over the valid ones). Bedrock's `Threading` (D-61): `Atomic<T>`, `Thread`, `Mutex` and
  `LockGuard`, `ConditionVariable`, `WaitGroup`, `Promise<T>` / `Future<T>`, and `JobSystem` (frame and
  background workers over lock-free bounded MPMC queues; waits that help; parked idle workers). Every attached
  thread installs the terminate handler; static methods decay to funcptrs (BUG-233). Tests: Bedrock
  `threading_tests.cf` (20), `threading_jobs_tests.cf` (10, among them the latency guarantee), IsolatedTest
  `Threads/UncaughtInThread`, `Threads/JobSystemUnderCollection`, `Types/AtomicsRuntimeOrders`,
  `Calls/StaticMethodPointers`.
- **Steps:** thread registry and suspend-and-scan in the GC; per-thread allocation caches; a
  thread-safe reflection registry; Bedrock `Thread`, `Mutex`, `ConditionVariable`, `Promise<T>` /
  `Future<T>`, a work-stealing job system; lift B7's `thread_local` restriction. we want threading to be optimized for latancy rather then through put so we dont get hitches from time sensitive tasks waiting for an async task to finish so it can take the worker thread. also the threading library in beadrock should be lock free. this is mainly for a game engine so preformance is key

#### B30 - ORCv2 scripting host
- **Design:** D-49, [scripting](../runtime/04-scripting.md). **Depends on:** B19, B29 (both done). **Status:** Done. NOTE: make sure all language features work across the scripting barrior including templates. we should also have compact information that goes with the build so you can compile a script without needing access to to original code. this includes the agressive inlining.
- **Feasibility (done):** a spike (since replaced by ScriptTest `Exceptions`) JIT-linked C4 and C++ modules with ORC (RuntimeDyld, one
  block per object, `.pdata` registered against the block's lowest section, an in-image personality thunk, a throw
  that passes the image base) and throws and catches in every direction. The one failure on the way was not the
  JIT: Frost compiles with `-fexternc-nounwind`, so an `extern "C"` host thrower had no unwind table.
- **Steps:**
  1. Deterministic symbols: template instances named for their arguments, lambdas for their function and
     position, compiler-written functions for their type and operation (no counters).
  2. Reflection registration emitted into the module as IR (the generated C file goes); a library build with no
     entry point (`Lower` with an empty entry, which the header documents but the code refuses).
  3. Runtime: script exit lists, emulated thread storage, root tables, dropping finalizers in a code range,
     guarded calls, stacked reflection registrations (`C4Runtime/ScriptRuntimeC.c`).
  4. `--script-host`: the symbol table (`__c4_script_table`), host thread_local accessors, the interface
     (`.c4i`: projects, files with interface text and exported macros, symbols, reflected types) and the inline
     bodies; `C4Script.dll` copied beside the output.
  5. `C4Script.dll`: the interface reader, script-mode registration and preprocessing, script-mode lowering,
     inline-body import, the optimizer pipeline, ORC linking (memory manager, unwind tables, EH glue, emulated
     TLS), load, guarded calls, unload.
  6. `Bedrock/Scripting` (`Host`, `Script`, `ScriptOptions`, `Guard`), docs, and the install (Nuke kit).
- **Tests:** C4Testing per brick (symbol determinism across two compiles of one program, the interface writer
  and reader, script-mode lowering, the table); a ScriptTest suite (`assets/ScriptTest/`, a host program plus
  scripts per case, at `-O0` and `-O2`) covering every row of the scripting page's section 2, reload, faults,
  the allow-list and diagnostics.
- **Done notes:** steps 1-4 as planned. Step 5: `Script/ScriptSources` (the interface's files registered under
  `//c4i`, their macros, the script's project), `Script/ScriptCompiler` (script-mode lowering: host functions
  declared, host storage external, `__c4_script_init`, the script's exit lists and throws, inline bodies imported
  `available_externally`, the PassBuilder pipeline), `Script/ScriptModulePasses`, `Script/ScriptJit` (one LLJIT per
  script, the host table as the symbol source), `Script/ScriptHost` (load, guarded calls, unload, reload; loads and
  unloads under one lock), `C4Script.dll` with its C API. A script's `thread_local` whose start runs code starts on
  first use per thread (`__c4_tls_start_<sym>`, called by the emitter where the address is taken, so it unwinds
  to the enclosing handler). `Script::RunTests` runs a script's `[test]` functions through a harness named
  `__c4_script_tests` (the harness runs only the program's own tests, and each run starts its counts from zero).
  Step 6: `Bedrock/Scripting` (`Host` with `Reload`, `Script`, `ScriptOptions`, `Guard`), the Nuke install ships
  `C4Script/`, docs. Found and fixed on the way: BUG-240 to BUG-249 - a host thread_local in a catch block lost its
  funclet, a script's RTTI could not reach host type descriptors (now the script's own, matched by name), a script's
  object of a host class used a vtable copy that died with the script (vtables are now named for their type and
  every class gets them in a script host), `T v(args);` outside a body and `int32 x(5);`, `(v) * (v)` read as a cast
  (C#'s rule now, with template type parameters known to the parser), a hidden script function was unreachable,
  and C bindings the host never called were not in its table. Tests: C4Testing `Script.Compiler` (18), the
  ScriptTest suite (17 cases), IsolatedTest `Lifecycle/DirectInitialization`, `Operators/ParenthesizedOperands`,
  NegativeTest `ScalarWithTwoArguments`, `ScalarGlobalWithTwoArguments`, `CastOfNegativeNeedsParentheses`.

#### B31 - Linux port
- **Depends on:** B21, A5 (done). **Status:** Ready. GC platform layer (`GC/GCPlatformPosix.c`: mmap, pthread stacks), SysV ABI in
  `Abi`, Itanium EH (landingpads) in codegen, ELF linking, `PLATFORM_LINUX`.

#### B32 - Performance validation
- **Depends on:** A5 (done), B20 step 6 (done). **Status:** Done.
- **Done notes:** SIMD math vs GLM (`Benchmarks/math/`); GC pause and throughput under game-like churn
  (`Benchmarks/gc/`) and compile times (`Benchmarks/compile/`), baselines in `docs/tools/05-test-suites.md` section 8.
  The GC benchmark found marking to be the whole pause (21 ms average for 52 MiB live), so marking is now parallel
  (`C4Runtime/GC/GCParallelMark`, root ranges queued for every marker): 6.2 ms average. The compile benchmark found
  `--time-phases` silent on success (BUG-234) and shows the LLVM optimizer and object step dominating `-O2`
  builds. Added `clock_now_ns()` (a monotonic clock, also behind the test harness's timings). Tests: IsolatedTest
  `MemoryGC/ParallelMarking`, ToolsTest `TimePhasesFlag`. Follow-ups on the roadmap: a concurrent mark (3.11),
  in-process object emission and parallel code generation (5.1).

#### B38 - Projects: several projects in one build
- **Design:** D-59, [projects](../language/15-projects.md) (from the owner's notes, 2026-10-19). **Depends on:** B21 (done).
  **Status:** Done.
- **Done notes:** `Compiler/ProjectFile` and `Compiler/Projects` (the description read and checked);
  `SourceManager` records each source's project and resolves includes from it (`ResolveInclude`: beside the
  file, its project, its dependencies, the library; ambiguity and boundary errors; the undocumented basename
  fallback is gone); `Compiler/SourceMacros` and `MacroPass::RunFiles` start each file with its project's
  defines, and an include carries the `exported` table (what the file defines, not its project's settings);
  `ProgramModel::OutOfProjectReach` bounds declaration-level names; interop searches each file's project
  folders; `C4.exe --projects`; the LSP's `projects` option (and documents and URIs map by absolute path, which
  also fixed `-I` and Bedrock files in the editor); Nuke writes `projects.toml` (`Nuke/ProjectsFile.py`). A plain
  command line stays one project. Tests: C4Testing `Compiler.Projects`, `SourceManager.Projects`, `Lsp.Server`;
  ToolsTest `ProjectDefines` (with inlining across projects at -O2), `ProjectBoundaries`, `NukeKitProject`.
- **What is wanted:** `-I <dir>` compiles a dependency project together with the program (whole-program
  inlining across projects stays), but each project should keep its own settings and boundaries:
  - per-project definitions (`-D`), include roots and other options;
  - a project cannot reach into the project that includes it (a reusable library must not call something of
    its includer), so reusable projects stay reusable.

#### B33 - Self-hosting
- **Depends on:** everything above. **Status:** Blocked.

---

## 6. Dependency summary

```text
Architect, in order:  A0 -> A1 -> A2 -> A3 -> A4 -> A5 -> A6 -> A7 -> A8 -> A9 -> A10
Done:                 B1 B2 B3 B4 B5 B6 B7 B8 B9 B10 B11 B12 B13 B14 B15 B16 B17 B18 B19 B20 B21 B22 B23 B24 B25 B27 B28 B30 B34 B35 B36 B37 B38 B39 B40 B41
Ready now:            B31
After A1:             B10 B15 (done: A1)     After A2:       B7 B34 B35 (done: A2)
After A3:             B11 (done: A3)         After A3 + A4:  B20(3-6) (done: A4)
After A3 + A6:        B8
After A7:             B9                     After B3:       B2 (done: B3)
After B3 + B22:       B24                    After B4:       B18 (done: B4)
After A2 + A4:        B19 (done: A4)         After B21 + A5 + B12: B23 (done: B21)
After A5:             B29 (done: A5) -> B30, B31, B32   After A1..A3:   B28 (done: A3)
After everything:     B26 (code health sweep), B33
```
