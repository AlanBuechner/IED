# 3. Lexical structure

Source files are UTF-8. Outside comments and literals the text is ASCII; inside them any UTF-8 is
allowed (a `char8` literal keeps the bytes, a `u""` / `U""` literal re-encodes them, 3.9). The lexer lives
in `C4Lib/src/Lexer/` (literals: `LiteralScanner.cpp`, `QuotedLiteral.cpp`). A lexical error (a bad
literal, an unclosed comment) is reported before the parse errors it causes.

---

## 3.1 Comments and whitespace

**Status:** **Implemented**

- `// line comment`
- `/* block comment */` (not nested). `/*/` does not close it (the `*` belongs to `/*`); the shortest
  block comment is `/**/`. A `/*` with no `*/` is the error `this '/*' comment has no closing '*/'`.
- Whitespace: space, tab, CR, LF. Newlines are not significant (except that preprocessor directives are
  one line each, see [02-preprocessor](02-preprocessor.md)).

## 3.2 Keywords

**Status:** **Implemented**

Reserved words (cannot be used as identifiers):

| Group | Keywords |
|---|---|
| Declarations | `namespace` `class` `struct` `interface` `enum` `union` `alias` `template` `extern` `funcptr` |
| Access | `public` `private` `protected` |
| Methods | `virtual` `override` `extends` |
| Storage | `const` `static` |
| Control flow | `if` `else` `switch` `case` `default` `fallthrough` `for` `while` `do` `break` `continue` `return` `defer` `goto` `try` `catch` `finally` `throw` |
| Assembly | `asm` ([16](16-inline-assembly.md)) |
| Objects / memory | `new` `delete` `this` |
| Type operators | `is` `as` |
| Literals | `true` `false` `null` `nullptr` (`nullptr` is a synonym for `null`) |

`loop` is an ordinary identifier (D-04). Type names, built-in aliases included (`int32`, `int`,
`char`), are not keywords: they are names the type lookup resolves ([04-types](04-types.md#41-primitive-types)).

Not keywords in C4 (compared to C++): `auto`, `using`, `typedef`, `operator`, `friend`, `typename`
(only contextual), `sizeof` (contextual), `string`. There is no `swizzle` keyword; vector lanes are
declared with `packed` / `unpacked` (see [11-vectors-and-swizzling](11-vectors-and-swizzling.md)).

## 3.3 Contextual identifiers

These are ordinary identifiers that carry meaning only in specific positions:

| Identifier | Where | Status |
|---|---|---|
| `include` | file root: `include "x.ch"` | Implemented |
| `packed`, `unpacked` | lane views inside a union; `packed T[N]`, a SIMD vector value type ([11 7](11-vectors-and-swizzling.md#7-packed-arrays-as-values)) | Implemented |
| `sizeof`, `__uuidof` | expression | Implemented (see [10-expressions](10-expressions.md)) |
| `static_cast`, `dynamic_cast`, `reinterpret_cast`, `const_cast` | expression | Implemented: each allows what C++'s does (see [10-expressions](10-expressions.md#4-unary-expressions)) |
| `typename`, `class` | template parameter | Implemented |
| `source`, `define`, `include_dir`, `lib` | `extern` interop blocks | Implemented (see [13-interop](13-interop.md)) |
| `abstract`, `final` | before a class kind, and as method modifiers | Implemented (see [05-objects](05-objects.md#59-virtual-methods)) |
| `noexcept` | trailing function qualifier | Implemented (see [07-functions](07-functions-and-lambdas.md#71-function-declarations)) |
| `volatile`, `thread_local` | variable modifiers | Implemented (see [04-types](04-types.md#413-storage-modifiers-volatile-and-thread_local)) |

`concurrent` is not a C4 modifier: writing it is the error `'concurrent' is not a C4 modifier` (D-29).

## 3.4 Operators and punctuation

**Status:** **Implemented**

Single-character: `{ } [ ] ( ) : ; , . + - * / % ~ ^ = < > & | ! ?`

Multi-character: `:: ++ -- -> == != <= >= && || += -= *= /= %= &= |= ^= << >> <<= >>= ** .. ...`

A multi-character operator is the longest run of adjacent characters that forms one (maximal munch,
as in C++). Whitespace or a comment ends it: `* ==` is `*` then `==`, and `< =` is `<` then `=`.

The precedence of these operators is documented in [10-expressions](10-expressions.md).

Lexed but unused by the grammar (any use is a parse error): `@`, `@@`, `\`, `\\`, `:=`, `~=`, `||=`.

`..` and `...` are used for ranges in initializer maps (`{ [2...5] = 42 }`) and for variadic parameters.

**Gotcha:** `**` is a single token (power operator, which has no lowering yet). So `a**p` meaning
`a * *p` lexes as `a ** p` and fails. Write `a * *p`. In a type, `**` is split back into two pointer
layers, so `void** p` works.

## 3.5 Identifiers

Letters, digits and `_`, not starting with a digit. Identifiers are case-sensitive. By convention
(Bedrock, tests): `PascalCase` types and functions, `camelCase` locals and fields.

## 3.6 Integer literals

**Status:** **Implemented**

| Form | Example |
|---|---|
| Decimal | `42` |
| Hexadecimal | `0xFF`, `0X1f` |
| Binary | `0b1010`, `0B1` |
| Octal (a leading `0`, as in C) | `010` (= 8) |

The type is C's: the first type in the list that holds the value.

| Suffix | Decimal | Hexadecimal, binary, octal |
|---|---|---|
| none | `int32`, `int64`, `uint64` | `int32`, `uint32`, `int64`, `uint64` |
| `u` / `U` | `uint32`, `uint64` | `uint32`, `uint64` |
| `l` / `L` / `ll` / `LL` | `int64`, `uint64` | `int64`, `uint64` |
| `ul`, `ull`, `lu`, `llu` (any case, `ll` not mixed) | `uint64` | `uint64` |

- `0xFFFFFFFFFFFFFFFF` is a `uint64`; `2147483648` is an `int64`; `0xFFFFFFFF` is a `uint32`.
- A value past `uint64` is the error `integer literal '...' is too large`.
- A literal is one token up to its last letter or digit, and each malformed one is an error that says
  why: `10q` (invalid suffix), `1f` (`'1f' is an integer: ... write 1.0f`), `09` (octal cannot hold 9),
  `0b12`, `0x` (no digits).
- `-5` is the operator `-` applied to `5`. No digit separators.

## 3.7 Floating-point literals

**Status:** **Implemented**

| Form | Example | Type |
|---|---|---|
| With `.` | `1.5`, `3.0` | `double` |
| With exponent | `1e5`, `1.5e-3` | `double` |
| `f` / `F` suffix | `1.5f`, `2.0F` | `float` |

- A float needs a digit before its `.`: `.5` is the error `'.5' needs a digit before the '.': write 0.5`.
  `1.` is `1` then `.`.
- A value out of range is an error (`1e39f` for `float`, `1e999` for `double`).

## 3.8 Character literals

**Status:** **Implemented**

| Literal | Type |
|---|---|
| `'a'`, `u8'a'` | `char8` (`char` is its alias) |
| `u'a'` | `char16` |
| `U'a'` | `char32` |

- A character literal holds exactly one character that fits one code unit: `''` and `'ab'` are errors,
  and so is `'\u00e9'` (two UTF-8 bytes): write `u'\u00e9'` or `U'\u00e9'`. An escape that names a code
  unit (`'\xFF'`, `'\377'`) always fits.
- `char8` is signed, like C's `char` on Windows: `'\xFF'` is `-1`. `char16` and `char32` are unsigned.

## 3.9 String literals

**Status:** **Implemented**

| Literal | Type |
|---|---|
| `"..."`, `u8"..."` | `char8*` (UTF-8) |
| `u"..."` | `char16*` (UTF-16) |
| `U"..."` | `char32*` (UTF-32) |

```c4
const char* s = "hello\n";
char16* w = u"caf\u00e9";            // UTF-16 code units, NUL-terminated
const char* joined = "ab" "cd";      // adjacent strings join: "abcd"
```

- A string literal is a NUL-terminated array of code units in a private global; its value is the
  address of the first unit. C4 deliberately has **no built-in string type**; owning UTF-8 strings come
  from the library (`Core::String`, `Core::StringView`, `Core::StringBuilder` in
  [../library/bedrock.md](../library/bedrock.md)).
- An embedded `\0` is kept: `"a\0b"` is three units and a terminator.
- Adjacent string literals join, as in C, when they have the same prefix (none and `u8` are the same).
- A literal ends on its line: a newline before the closing quote is the error
  `missing the closing '"' of a string literal on this line`.
- Not supported: raw strings (`R"(...)"`) and `L"..."` (C4 has no `wchar_t`; use `u"..."`).

## 3.10 Escape sequences

Valid in character and string literals:

| Escape | Meaning |
|---|---|
| `\n` `\t` `\r` `\a` `\b` `\f` `\v` | newline, tab, carriage return, bell, backspace, form feed, vertical tab |
| `\e` | escape (0x1B) |
| `\\` `\"` `\'` `\?` | backslash, quote, apostrophe, question mark |
| `\0`, `\nnn` | octal code unit, one to three digits (`\0` is NUL) |
| `\xHH` | hex code unit: at most 2 digits in a `char8` literal, 4 in `char16`, 8 in `char32` |
| `\uXXXX`, `\UXXXXXXXX` | the code point, exactly 4 / 8 hex digits, encoded in the literal's width (UTF-8 bytes in a `char8` string, a surrogate pair in `char16` past U+FFFF) |

- `\x` stops after its digit count: `"\x41B"` is `A` then `B`.
- An octal escape larger than the code unit (`"\777"` in `char8`) is an error.
- `\u` / `\U` must name a code point up to U+10FFFF that is not a surrogate (D800-DFFF).
- Any other escape is the error `unknown escape sequence '\q'`.

## 3.11 Boolean and null literals

**Status:** **Implemented**

- `true`, `false` - type `bool`.
- `null` (and its synonym `nullptr`) - a `void*` zero that converts to any pointer or `funcptr` type.
  C4 code uses `null`.

## 3.12 Summary of lexical gaps

| Gap | Status |
|---|---|
| Raw strings, digit separators | Not planned |
| `**` power operator | Parsed only (see [10-expressions](10-expressions.md)) |
