# What is C4

C4 is a mostly C++-like compiled language for game development. It has some syntax changes and some
additions. Outside of the changes listed in these docs, **C4 should behave exactly like C++**: if the
docs do not say something is different, the C++ rule is the intended rule.

The compiler (`C4.exe`) is built on LLVM and produces native Windows x64 executables. Programs link
against a small C runtime (garbage collector, reflection registry, test runtime, file IO) and can use
the Bedrock default library.

---

## Goals

These come from the project owner's own statements (`AGENTS.md`, `LongTermPlan.txt`):

| Goal | What it means |
|---|---|
| **Familiar to C++ programmers** | Same mental model: value types, pointers, references, templates, RAII. Learning C4 means learning the list of differences, not a new language. |
| **Built for games** | SIMD vector types with HLSL-style swizzling in the language, runtime reflection for editors and serialization, a GC with hooks for engine allocators, built-in testing. |
| **C++-level performance** | Everything should be as fast as well-optimized C/C++. Performance claims are checked against equivalent C++ (for example `Benchmarks/gemm`). |
| **Fast, simple builds** | The whole program is compiled as one unit. Each file is parsed exactly once. Headers are not re-parsed per includer and there is no separate link step to manage. |
| **Helpful errors** | Invalid code must produce an error that helps find and fix the problem. The compiler must never crash on bad input. |
| **No built-in string type** | The language only has `char*`. Strings (UTF-8) come from the default library. |
| **Seamless C/C++ interop** | Include C and C++ headers and sources directly; match the platform ABI so calls cross without glue. |
| **Exhaustive testing** | Every feature, every interaction between features, and every error path is tested. A failing test is fixed, never weakened. |

Long-term direction (see [status/roadmap.md](status/roadmap.md)): built-in scripting with LLVM ORCv2
JIT, threading primitives, a math library, a debugger (DAP), Linux support,
self-hosting (C4 written in C4) and porting a game engine to C4.

## Design principles

- **One concept instead of several.** Constructors, destructors, operators and iteration hooks are all
  ordinary methods tagged with `[operator("...")]`. Libraries call any operator generically
  (`v.operator_hash()`) without naming conventions.
- **The type says everything about the type.** Array sizes, pointer layers and `const` belong to the
  type, not the variable name: `const int32[10] arr;`.
- **Headers are source files.** An included header is compiled once and is the same for everyone who
  includes it. A `static` variable in a `.ch` is defined there. No forward declarations are needed at
  file scope.
- **Visibility you can see.** Members carry C#-style inline modifiers (`public void F();`); C++-style
  section labels are allowed for grouping but do not replace the inline modifier.
- **Single inheritance, multiple interfaces.**
- **Memory is managed by default, controllable when it matters.** Every `T*` is a garbage-collected
  pointer, value-type locals use RAII, and raw allocation plus GC hooks exist for engine code.
- **Strong architecture over special cases** (for the compiler itself): one program model answers every
  semantic question; see [compiler/README.md](compiler/README.md).

---

## C4 vs C++ at a glance

| Topic | C++ | C4 | Page |
|---|---|---|---|
| Compilation | Per translation unit, then link | Whole program at once; every file parsed once | [program model](language/01-program-model.md) |
| `#include` | Textual, depends on what was defined before it | `include "x.ch"` is a declaration; the header is the same for every includer; macros still flow to the includer | [program model](language/01-program-model.md), [preprocessor](language/02-preprocessor.md) |
| Forward declarations | Needed for types and functions | Not needed for types at file scope; functions implemented in another file still need a declaration | [program model](language/01-program-model.md) |
| Templates in headers | Bodies must be in the header | Bodies can live in a `.cf` | [templates](language/08-templates.md) |
| Arrays | `const int a[10];` | `const int32[10] a;` | [types](language/04-types.md) |
| Constructors / destructors | `T()`, `~T()` | `[operator("create")]`, `[operator("delete")]` on normal methods | [lifecycle](language/06-lifecycle-and-operators.md) |
| Operators | `operator+` | `[operator("+")]` or `[operator("add")]` on a normal method; custom operators like `"hash"` | [lifecycle](language/06-lifecycle-and-operators.md) |
| Iterators | `begin()` / `end()` naming convention | `[operator("begin")]` / `[operator("end")]` | [lifecycle](language/06-lifecycle-and-operators.md) |
| Function pointers | `int (*f)(int)` | `funcptr<int32(int32)> f` | [functions](language/07-functions-and-lambdas.md) |
| Lambdas | Return type optional | `[](int32 x) -> int32 { ... }`; `->` required when returning a value | [functions](language/07-functions-and-lambdas.md) |
| Inheritance | Multiple | One base class plus any number of interfaces | [objects](language/05-objects.md) |
| Visibility | Section labels | Inline modifiers, optionally grouped under section labels | [objects](language/05-objects.md) |
| Adding methods to a type you do not own | Not possible | Extension methods: `float Length() extends Vec2 { ... }` | [objects](language/05-objects.md) |
| Type aliases | `using` / `typedef` | `alias Name = Type;` | [types](language/04-types.md) |
| SIMD vectors | Intrinsics or libraries | `packed` lane views with swizzles (`v.xz = {1, 1}`) | [vectors](language/11-vectors-and-swizzling.md) |
| Scope exit | RAII helper objects | `defer stmt;` | [control flow](language/09-statements-and-control-flow.md) |
| Exceptions | `try` / `catch` | Also `finally` | [control flow](language/09-statements-and-control-flow.md) |
| Heap memory | Manual / smart pointers | `new` returns a GC-managed pointer; `delete` is still allowed | [memory](runtime/01-memory-and-gc.md) |
| Reflection | None | Built in: types, fields, methods, enums, globals, functions | [reflection](runtime/02-reflection.md) |
| Unit tests | External framework | `[test("group")]` on static functions, `-test` builds a harness | [testing](runtime/03-testing.md) |
| Strings | `std::string` | No language string; `char*` literals, Bedrock `Core::StringView` / `Core::String` | [Bedrock](library/bedrock.md) |
| Integer type names | `int`, `long`, ... | Explicit widths: `int8` ... `int64`, `uint8` ... `uint64` | [types](language/04-types.md) |

---

## A short tour

A header and a source file. The header declares; the source implements.

```c4
// geometry.ch
struct Vec2 {
	public float x;
	public float y;

	[operator("+", "add")]
	public Vec2 Add(const Vec2& other);
};

float Length() extends Vec2;
```

```c4
// geometry.cf
include "geometry.ch"

Vec2 Vec2::Add(const Vec2& other) {
	Vec2 result = { x + other.x, y + other.y };
	return result;
}

float Length() extends Vec2 {
	return sqrtf(this.x * this.x + this.y * this.y);
}
```

A class with a constructor and a destructor. Value-type locals run them automatically (RAII):

```c4
// counter.ch
class Counter {
	int32 value;

	[operator("create")]
	public void Init() { value = 0; }

	[operator("delete")]
	public void Shutdown() { printf("counter done at %d\n", value); }

	public void Inc() { value++; }
	public int32 Get() { return value; }
};
```

The entry point, using Bedrock and a range-for:

```c4
// main.cf
include "bedrock.ch"
include "geometry.ch"
include "counter.ch"

int32 main() {
	Vec2 a = { 1.0f, 2.0f };
	Vec2 b = { 2.0f, 2.0f };
	Vec2 c = a + b;
	printf("c = (%f, %f), |b| = %f\n", c.x, c.y, b.Length());

	Counter counter;                  // runs Init()
	int32[4] values = { 1, 2, 3, 4 };
	for (int32 v : values) {
		counter.Inc();
	}

	Core::Array<int32> list;
	list.Push(counter.Get());
	return 0;                         // counter's Shutdown() runs here
}
```

A unit test, collected when compiling with `-test` (or `nuke test`):

```c4
// tests/geometry_tests.cf
include "../geometry.ch"

[test("geometry.vec2.add")]
public static void TestVec2Add() {
	Vec2 a = { 1.0f, 2.0f };
	Vec2 b = { 3.0f, 4.0f };
	Vec2 c = a + b;
	assert_feq(4.0, c.x, 0.0001, "x adds");
	assert_feq(6.0, c.y, 0.0001, "y adds");
}
```

Every `.cf` / `.ch` file in the source folder is part of the program. `include` controls which names
a file can see, not what gets compiled, and an included header is never re-parsed (see
[program model](language/01-program-model.md)).

---

## Toolchain at a glance

| Piece | What it is | Page |
|---|---|---|
| `C4.exe` | The compiler: sources in, linked `.exe` out | [tools/01-compiler-cli.md](tools/01-compiler-cli.md) |
| Bedrock | Default library, shipped next to `C4.exe`, used with `include "bedrock.ch"` | [library/bedrock.md](library/bedrock.md) |
| C4Runtime | C runtime linked into every program (GC, reflection, tests, IO, exceptions) | [runtime/](runtime/01-memory-and-gc.md) |
| Nuke | Build system for C4 projects (`nuke build / run / test / init`), with versioned C4 toolchains | [tools/02-nuke.md](tools/02-nuke.md) |
| CBind | Generates C4 declarations from C/C++ headers for `extern` includes | [language/13-interop.md](language/13-interop.md) |
| C4LSP | Language server, for IED or any other LSP client | [tools/03-editor-lsp-dap.md](tools/03-editor-lsp-dap.md) |
| C4DAP | Debug adapter for programs built with `-g`, for IED or any other DAP client | [tools/03-editor-lsp-dap.md](tools/03-editor-lsp-dap.md) |
| Frost | C++ build system used to build the compiler itself | [tools/04-building-the-compiler.md](tools/04-building-the-compiler.md) |

**Platform:** Windows x64 only today (MSVC ABI, Win32 GC runtime). Linux is planned.
