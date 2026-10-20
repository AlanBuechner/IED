# 2. Preprocessor (macros)

C4 keeps C-style `#define` macros and conditional compilation, but `include` is NOT part of the
preprocessor (see [01-program-model](01-program-model.md#12-include)). Macros run as a text pass over each
file before it is lexed. Each file is expanded once, in isolation, and the final macro table of every
file it includes is merged in. The default library next to `C4.exe` goes through the same pass.

Implementation: `C4Lib/src/Macro/` - `MacroPass` (files and includes), `MacroPreprocessor` /
`MacroDirectives` (lines and directives), `MacroExpander` / `MacroSubstitution` (expansion),
`MacroExpression` (`#if`), `ConditionalStack`, `PredefinedMacros`.

---

## 2.1 Object-like and function-like macros

**Status:** **Implemented**

```c4
#define MACRO_CONST 42
#define FLAG (1 << 3)                    // object-like: a space before the (
#define DOUBLE(x) ((x) * 2)              // function-like: the ( right after the name
#define TRIPLE(x) (DOUBLE(x) + (x))      // macros compose
#define NOW() 7                          // takes no arguments: NOW()

MACRO_CONST t = FLAG;
int32 c = DOUBLE(2) + DOUBLE(3);         // 10
```

- A macro is function-like only when `(` follows its name with no space (C's rule): `#define FLAG (1)` is
  object-like.
- Parameters are replaced all at once, never inside string or character literals of the body:
  `#define F(a, b) a + b` makes `F(b, 1)` be `b + 1`.
- An argument is fully expanded before it replaces its parameter, and the result is rescanned for more
  macros. A macro never expands inside its own expansion (`#define X X + 1` makes `X` be `X + 1`).
- A call may continue on the following lines: `ADD(1,` on one line and `2)` on the next is one call.
- A call with the wrong number of arguments is an error: `the macro 'F' takes 2 arguments, but 1 was given`.
- Names inside string literals, character literals and comments are never expanded.
- A trailing comment on a directive line (`//` or `/* */`) is not part of the body.
- `#undef NAME` removes a macro; it may then be redefined. Redefining a macro with a different body
  without `#undef` is a warning (`'X' is redefined: #undef it first`).
- `defined`, `__FILE__` and `__LINE__` cannot be defined.

## 2.2 Stringize, paste and variadic macros

**Status:** **Implemented**

```c4
#define STR(x) #x                 // STR(a + b) is "a + b"
#define XSTR(x) STR(x)            // XSTR(VERSION) is "3" when VERSION is 3
#define CAT(a, b) a ## b          // CAT(my, Var) is myVar, CAT(1, 2) is 12
#define FIELD(name) int32 m_##name

int32 SumAll(int32 n) { return n; }
#define CALL(...) SumAll(__VA_ARGS__)
int32 v = CALL(41);               // SumAll(41)
int32 t = CALL(9,);               // SumAll(9): an empty trailing argument of a variadic macro is dropped
```

- `#param` (function-like macros only) is the argument as written in quotes: spaces collapse to one,
  quotes and backslashes inside its literals are escaped, and the argument is not expanded first. `#`
  before anything that is not a parameter is an error.
- `a ## b` joins the tokens on either side; its operands are not expanded first. `##` cannot start or end
  a body. It also works in object-like macros.
- `__VA_ARGS__` is the variadic arguments joined with `", "`. `#define F(a, b, ...)` (named parameters
  then `...`) is supported. Because forwarding an empty `__VA_ARGS__` often leaves a dangling comma, a
  trailing comma before `)` in a call argument list is accepted by the parser.

## 2.3 Conditional compilation

**Status:** **Implemented**

Directives: `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`, nested to any depth. Code in an
inactive branch is skipped as text, so it may contain anything (it is never parsed), and the conditions
of groups nested in it are never evaluated.

```c4
#define LEVEL 2
#if defined(FOO) && LEVEL >= 2 || !defined BAR
int32 a = 100;
#elif LEVEL == 1
int32 a = 1;
#else
int32 a = 999;
#endif
```

A condition is C's integer constant expression, evaluated over `int64`:

1. `defined(X)` and `defined X` become 1 or 0 (before macros expand).
2. Macros expand.
3. A name left after expansion is 0; `true` is 1, `false` is 0. `#define X 0` then `#if X` is false.
4. Operators, with C's precedence: `! ~ - +` (unary), `* / %`, `+ -`, `<< >>`, `< <= > >=`, `== !=`, `&`,
   `^`, `|`, `&&`, `||`, `?:`, and parentheses. Integer literals of every radix and suffix, and
   character literals, are allowed; a floating-point literal is an error.
5. `&&`, `||` and `?:` do not evaluate the operand they skip: `#if 0 && 1 / 0` is false, not an error.

Errors: an empty or malformed condition, division or remainder by zero, a shift count outside 0..63, a
`#else` / `#elif` / `#endif` with no open `#if`, a second `#else`, `#elif` after `#else`, text after
`#else` / `#endif`, and a `#if` with no `#endif` at the end of the file.

## 2.4 Lines, comments and other directives

**Status:** **Implemented**

- A directive continues on the next line after a trailing `\`. Every line of the file stays one line of
  the expanded text, so error locations are unchanged.
- A `#` line inside a `/* ... */` comment that spans lines is comment text, not a directive.
- `#error message` stops the build with the message; `#warning message` reports a warning.
- `#pragma` is accepted and ignored (`#pragma once` is a no-op: every file is parsed once anyway).
- `#line` is an error (C4 keeps each file's own line numbers). `#include` is an error that points at the
  `include` declaration. Any other directive is the error `unknown directive '#name'`. `#` alone is
  allowed.

## 2.5 Macros and includes

**Status:** **Implemented**

Macro state flows *down* the include chain: when file B includes header A, the macros A defines are
merged into B's table, so B sees every `#define` that A (and everything A includes) left defined. The macros
A started with are not part of that: the predefined ones are B's own too, and the defines of A's project stay
A's ([15](15-projects.md)), so an engine header does not hand its project's settings to the game.

```c4
// MacrosBase.ch
#define MACRO_CONST 42
#define MACRO_FN(x) ((x) + 1)

// Macros.cf
include "MacrosBase.ch"
int32 a = MACRO_CONST;      // 42, inherited
int32 b = MACRO_FN(10);     // 11
```

Macro state never flows *up* or *sideways*:

- A header is expanded with only the predefined macros plus what its own includes provide. A `#define`
  written before `include "x.ch"` does NOT affect how `x.ch` is expanded (header isolation, see
  [01-program-model](01-program-model.md#13-program-model-rules)).
- A `#define` affects only the text after it in the current file and in files that include this one.

`-export-macros` writes each file's final macro table to `<name>.c4macro` next to the output (used by
tooling). See [../tools/01-compiler-cli.md](../tools/01-compiler-cli.md).

## 2.6 Predefined macros

**Status:** **Implemented**

| Macro | Value |
|---|---|
| `TEST` | `1` when building with `-test`, so tests and test helpers strip out of shipping builds |
| `PLATFORM_WINDOWS` | `1` (the only platform today) |
| `ARCH_X64` | `1` (the only architecture today) |
| `C4_VERSION` | `major * 10000 + minor * 100 + patch` (`C4.exe --version` prints the version) |
| `SIMD_SSE3`, `SIMD_SSSE3`, `SIMD_SSE4_1`, `SIMD_SSE4_2`, `SIMD_AVX`, `SIMD_AVX2`, `SIMD_FMA`, `SIMD_AVX512F`, `SIMD_AVX512VL`, `SIMD_AVX512BW`, `SIMD_AVX512DQ` | `1` when the build's processor has the feature (`-march`, `-m<feature>`; [vectors 8](11-vectors-and-swizzling.md#8-the-simd-target)); not defined otherwise |
| `SIMD_FLOAT_LANES` | `4`, `8` or `16`: the floats in the target's widest vector register (SSE, AVX, AVX-512) |
| `__FILE__` | the file's name as a string literal (its path relative to its folder) |
| `__LINE__` | the line it is written on |

`-D NAME` (value `1`) and `-D NAME=VALUE` (also `-DNAME=VALUE`) define a macro for every file and may
replace a predefined one. Nuke passes its configuration's defines this way; each project's own defines go in the
build description instead ([15-projects.md](15-projects.md), [../tools/02-nuke.md](../tools/02-nuke.md)).

## 2.7 Guidance

- Prefer `const` globals, `enum class`, templates and inline functions over macros for values and code;
  macros are for conditional compilation, platform switches and the rare code-generation shortcut.
- Parenthesize macro bodies and parameters (`((x) * 2)`); expansion is textual.
- Keep macros that other files need in a header; they reach every includer automatically.

## 2.8 Tests

C4Testing `Macro/MacroExpressionTest`, `Macro/MacroExpanderTest`, `Macro/MacroPreprocessorTest`; TestCode
`Core/Macros.cf`, `Core/MacroConditions.cf`; IsolatedTests `Macros/*` (conditions, stringize, paste, line
continuation, predefined macros); NegativeTests for each directive error; ToolsTest `DefineFlag`.
