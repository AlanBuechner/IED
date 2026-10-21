# 1. Program model, files and namespaces

C4 compiles a whole program at once. There is no per-file compile-and-link step: every source file of
a program is parsed exactly once, all declarations go into one program-wide model, and the program is
lowered and linked as a single unit. This page describes what that means for the code you write.

See also: [02-preprocessor](02-preprocessor.md) (macros and how they flow through includes),
[../compiler/01-pipeline.md](../compiler/01-pipeline.md) (how the compiler implements this).

---

## 1.1 Source files

**Status:** **Implemented**

| Extension | Role |
|---|---|
| `.ch` | Header: declarations that other files include. A header is itself a full source file. |
| `.cf` | Source: definitions (function bodies, globals). |

A program is a set of folders and files handed to the compiler. A folder input is scanned recursively
for `.cf` / `.ch`; every registered folder is also an include root. The default library, Bedrock, is
always compiled into the program (see [../library/bedrock.md](../library/bedrock.md)), but its names are
only visible to files that `include "bedrock.ch"`.

Conventions (from AGENTS.md): one concept per file, files named after their primary type or feature,
shared declarations in a `.ch`, definitions in the paired `.cf`.

## 1.2 `include`

**Status:** **Implemented**

```c4
include "Player.ch"                  // beside this file
include "Framework/Assert.ch"        // from the project's source folder, wherever this file is
include "bedrock.ch"                 // the default library
```

- `include` is a declaration, not a textual preprocessor directive. It is only allowed at file root.
- The path is a string literal naming a `.ch` file; a trailing `;` is optional.
- Resolution, the first match winning: relative to the including file's directory (`..` works); the
  including file's project folders (the input folders and `-I` folders of a plain command line); the folders of
  the projects it depends on ([15](15-projects.md)); the default library (and, for a file of the default library,
  the library's root). A bare name finds a file only there: `include "math.ch"` does not pick some nested
  `x/math.ch`.
- So a file deep in a project names another by its path from the project's source folder, `include "web/rpc.ch"`,
  and never needs `..`; that is the convention for every project in this repository, the default library included
  (`include "Text/string.ch"` from `Bedrock/Crypto/`).
- An include cycle of any length is an error at the include that closes it (`Cyclic include detected: 'b.ch'
  includes this file (directly or through the files it includes)`); a file including itself says so.

What an include does: it makes the included file's declarations (and, transitively, everything that
file includes) visible to function bodies in the including file, and it carries the included file's
final macro table into the includer (see [02-preprocessor](02-preprocessor.md#25-macros-and-includes)).

## 1.3 Program model rules

These rules are the core difference from C/C++ (AGENTS.md "Changes" item 1).

**Status:** **Implemented**

1. **Every includer sees the same header.** A header is parsed once, on its own. Defining a macro or a
   type before an `include` does NOT change how that header is parsed.
2. **No forward declarations for types at file scope.** Types, functions and globals may be used before
   the point where they are declared in a file, or in a file that is never included, for declaration
   purposes (field types, signatures). Inside a function body, statements are processed in order, so a
   type declared locally in a body must come before its use.
3. **A header is a source file.** A `static` variable (or any global) defined in a `.ch` is defined
   right there - there is no separate "definition in one `.cf`" step. It exists once in the program no
   matter how many files include the header.
4. **Functions implemented elsewhere still need a declaration** in a header, so that includers know the
   function exists.
5. **Template bodies may live in a `.cf`**, like any other function body. They do not have to be in the
   header.

```c4
// OrderIndependence: no forward declaration needed
struct OI_A { public OI_B b; public int32 x; };   // OI_B is defined later
struct OI_B { public int32 y; };
```

### Differences from C++

| C++ | C4 |
|---|---|
| `#include` pastes text; each translation unit re-parses headers | `include` is a declaration; each file is parsed once |
| A `#define` before `#include` can change the header | It cannot |
| `extern int g;` in the header + `int g;` in one `.cpp` | Define `int32 g;` in the header; done |
| Forward-declare types to break cycles | Not needed at file scope |
| Templates must be defined in headers | Template bodies may be in `.cf` |
| ODR violations are often silent | Duplicate definitions are hard errors |

## 1.4 Visibility: declarations vs bodies

**Status:** **Implemented**

Two different rules apply:

- **Declaration-level resolution is program-wide.** The type of a field, a parameter, a return type or
  a base class resolves against the whole program. A header may name a type declared in a sibling header
  it never includes.
- **Body-level resolution is include-closure checked.** Inside a function body, a name is visible only if
  the file that declares it is in the transitive include closure of the file containing the body. For a
  function, its *declaration's* file counts, so including the header that declares it is enough even if
  the body lives in some `.cf`.

Including a header therefore matters for code inside bodies; it is not needed to make declarations line up.

**Planned:** private-to-file declarations beyond `static` (for example header-private symbols) are not
part of the language today.

## 1.5 File-scope `static`

**Status:** **Implemented**

A file-scope `static` function has internal linkage: two files may each define `static void Probe()`
and they are distinct functions, and a call in each file reaches its own. The same holds inside a namespace, which
every file that opens it shares: `namespace N { static int32 Pick(int32 x); }` in two files is two functions (BUG-289).

```c4
// a.cf
int32 tag_a = 0;
public static void Probe() { tag_a = 1; }

// b.cf
int32 tag_b = 0;
public static void Probe() { tag_b = 1; }
```

## 1.6 One definition per program

**Status:** **Implemented**

Because the whole program is one model, duplicates are hard errors with a clear message:

| Situation | Diagnostic |
|---|---|
| Two types with the same qualified name | `redefinition of type 'Pair'` |
| Two globals with the same name (including a declaration-only `float PI;` in one file and `float PI = 3.14f;` in another) | `redefinition of variable 'PI'` |
| The same function signature defined twice | `Duplicate definition of 'f'` |
| The same name declared twice in one block | `Variable 'x' is already declared in this block` |

There are no C-style tentative definitions: a global is declared and defined in one place.

## 1.7 Order independence and constants

**Status:** **Implemented**

File-scope declarations are order independent: the compiler registers every declaration first and then
resolves layouts, signatures and constant values to a fixed point. Constant initializers may refer to
constants declared later, and array sizes fold regardless of order:

```c4
const int32 CE_D = CE_M * 2;    // 30
const int32 CE_M = CE_N + 5;    // 15
const int32 CE_N = 10;
```

- A name in a constant initializer, an array extent or a lane count resolves from where it is written,
  as in code: inside `namespace Math`, `TAU = PI * 2.0f` finds `Math::PI`, a class static finds its
  siblings, and `Inner::J` finds the `Inner` nested in an enclosing namespace.
- A `const` whose initializer does not fold (it calls a function, builds an object, or reads a mutable
  global) is set once at startup ([04](04-types.md)): it is read-only but not a compile-time constant, so
  an array size naming it is an error at the size (`the array size 'kN' is not a constant expression: 'kN'
  starts at runtime: 'Seed()' is not one of the forms a constant folds from`).
- The same holds in every position a constant is needed: a parameter's array size, a default argument, an
  enum value, a template argument, an alias, a field or static member, a `case` label, a lane count, in this
  file or another, before or after the declaration. `c ? a : b` folds too.
- Constants that refer to each other in a loop are an error at each of them: `the constant 'A' depends on
  itself: A -> B -> A`.

## 1.8 Entry point

**Status:** **Implemented**

A runnable program defines a lowercase global `main`:

```c4
int32 main() { return 0; }
int32 main(int32 argc, char** argv) { return argc; }
```

- `main` may also return `void` (the process exits with 0), and reaching the end of an `int32 main`
  returns 0, as in C.
- The entry point is the file-scope free function named `main`; `main_helper` or a namespaced `Game::main`
  is an ordinary function.
- The return value is the process exit code.
- Missing `main`: `No entry point: the program has no 'main' function`.
- `Main` (capitalized) instead of `main`: `Entry point must be named 'main' (lowercase); you defined 'Main'`.
- Test builds (`-test`) need no `main`: the compiler synthesizes a test harness entry point. If the
  program also defines `main`, normal builds use it and `-test` builds still use the harness. See
  [../runtime/03-testing.md](../runtime/03-testing.md).

## 1.9 Namespaces

**Status:** **Implemented** (with the gaps listed)

```c4
// math.ch - shared by every file
namespace Math {
	struct Vec2 { float x; float y; }
	int32 Count();
	float Scale(float f);
}

// a.cf
include "math.ch"
namespace Math {
	int32 Count() { return 7; }
}

// b.cf
include "math.ch"
namespace Math {
	float Scale(float f) { return f * 2.0f; }
}

int32 main() {
	Math::Vec2 v; v.x = 1.0f;
	return Math::Count() == 7 ? 0 : 1;
}
```

- Namespaces are **open** and merge across files, like C++.
- Namespaces nest (`namespace A { namespace B { ... } }`); names are qualified with `::` (`A::B::F()`).
- Out-of-line definitions may be written qualified: `void Ns::Type::Method() { ... }`.
- `namespace A::B { }` is shorthand for `namespace A { namespace B { } }` (D-51): the body is in `A::B` and
  sees `A`'s names unqualified.
- There is no `using` / `using namespace`.
- Bedrock's names live in one-level namespaces (`Core`, `FS`, `Serialization`, `Reflection`, `GC`, and
  `Math` for the opt-in math library); namespaces are open, so user code may add members to any of them
  (see [../library/bedrock.md](../library/bedrock.md)).

- A function template inside a namespace is called qualified from outside it like any function
  (`Math::Identity<int32>(x)`, `Math::Identity(x)`).
- There is no `using`; `alias` names a type from another namespace (D-51).

## 1.10 Summary of status

| Feature | Status |
|---|---|
| Whole-program compilation, parse once | Implemented |
| `include` with header isolation | Implemented |
| No file-scope forward declarations | Implemented |
| Globals/statics defined in headers | Implemented |
| Include-closure visibility in bodies | Implemented |
| File-scope `static` internal linkage | Implemented |
| Duplicate-definition errors | Implemented |
| Order-independent constants | Implemented (every position, TestCode `Core/ForwardConstUse`) |
| `main` entry point rules | Implemented |
| Open namespaces | Implemented |
| Qualified call of a namespaced function template | Implemented |
| `namespace A::B { }` | Implemented |
| `using` | Not part of the design (D-51) |
