# 4. Types

C4's type system is C++'s with three deliberate changes: type modifiers (arrays, pointers, const) are
written on the type rather than the variable, every pointer is a garbage-collected pointer, and there is
no built-in string type. Objects (`class`, `struct`) are covered in [05-objects](05-objects.md).

---

## 4.1 Primitive types

**Status:** **Implemented**

| Type | Size (bytes) | Notes |
|---|---|---|
| `void` | - | |
| `bool` | 1 | `true` / `false` |
| `int8` `int16` `int32` `int64` | 1 / 2 / 4 / 8 | signed |
| `uint8` `uint16` `uint32` `uint64` | 1 / 2 / 4 / 8 | unsigned |
| `float` | 4 | IEEE single |
| `double` | 8 | IEEE double |
| `char8` | 1 | byte character (a UTF-8 code unit), used for C strings (`char*`); signed, like C's `char` on Windows |
| `char16` `char32` | 2 / 4 | UTF-16 / UTF-32 code units, unsigned (`u'x'`, `U'x'`, `u"..."`, `U"..."`: [03-lexical-structure](03-lexical-structure.md#38-character-literals)) |
| `snorm8` `snorm16` `snorm32` `snorm64` | 1 / 2 / 4 / 8 | signed normalized: integer storage representing [-1, 1] |
| `unorm8` `unorm16` `unorm32` `unorm64` | 1 / 2 / 4 / 8 | unsigned normalized: integer storage representing [0, 1] |

**Built-in aliases** (D-01, D-02): `int` is `int32`, `uint` is `uint32` and `char` is `char8`. An alias
is the same type, not a new one: `int32*` points at an `int`, `F(int)` and `F(int32)` are one function
(two bodies are a duplicate definition), and diagnostics print the target name (`int32`). The LSP colors
the aliases as built-in types.

There is no `float32`, `float64`, `byte` or `size_t`: write the width you mean (`float`, `double`,
`uint8`, `uint64`).

### 4.1.1 Normalized integers

**Status:** **Implemented** (D-38)

A normalized integer stores a fraction as an integer, the way GPU vertex and texture formats do, so such
data can be described directly. `snormN` holds `[-1, 1]` as the integers `[-(2^(N-1)-1), 2^(N-1)-1]`;
`unormN` holds `[0, 1]` as `[0, 2^N-1]`. Its value computes in `float` for 8 and 16 bits and in `double`
for 32 and 64 bits.

```c4
snorm8 s = 0.5f;     // stores 64: 0.5 * 127 = 63.5, rounded to nearest (ties to even)
unorm8 u = 255;      // an integer is the stored encoding: u is 1.0
float f = u;         // 1.0f: the encoding over 255
float sum = s + u;   // arithmetic runs on the values, in float
unorm8 c = s + u;    // storing a value encodes it: 1.5 clamps to 255
int32 raw = (int32)s;  // 64: the stored integer
```

- A float converts by value: clamped to the type's range, scaled by `2^(N-1)-1` (snorm) or `2^N-1` (unorm)
  and rounded to nearest, ties to even. NaN stores 0.
- An integer converts as the encoding itself, clamped to the encoding range (`snorm8 s = 200;` stores 127,
  `unorm8 u = -3;` stores 0). Converting a normalized value to an integer gives the encoding.
- A normalized value converts to a float as encoding / scale; a snorm's lowest encoding (`-128` for
  `snorm8`) reads as `-1`. Between two normalized types the value converts (`unorm8` 64 is `unorm16` 16448).
- Arithmetic and comparisons with a normalized operand convert both operands to the normalized type's float
  type (or `double` when the other operand is a `double`); an integer operand is its value. The result is
  that float type; storing it back encodes it, so `u += 0.75f` clamps.
- Bit operators, `~`, `++` / `--` and `switch` do not take a normalized value, and a normalized constant is
  not folded into other constants (it is a fraction, not its encoding).

## 4.2 Declarators: modifiers belong to the type

**Status:** **Implemented**

In C4 the whole type is written before the name. Array extents, pointer layers, references and `const`
are all part of the type (AGENTS.md "Changes" item 2):

```c4
int32[10] arr;            // array of 10 int32
const int32[10] carr;     // array of 10 const int32      (C++: const int carr[10])
int32[4][3] m;            // 4x3 array ((int32 X 4) X 3)
int32* p;                 // pointer to int32
int32** pp;               // pointer to pointer
const int32* pc;          // mutable pointer to const int32
int32 const* cp;          // const pointer to mutable int32
const int32 const*[5] a;  // array of 5 const pointers to const int32
int32& r = x;             // reference to mutable int32
const Point& cr = pt;     // reference to const Point
char[4] bytes;            // NOT: char bytes[4]
```

Reading rule (D-30): a `const` qualifies the layer written after it. The leading `const` applies to the
base value (the referent of pointers and references); a `const` written after the base type, before a `*`,
makes that pointer layer const. The outermost layer's constness is the declared variable's, field's or
parameter's own:

- `const int32 n` and `int32 const* p` are read-only variables; `const int32* p`, a pointer to const, is
  not (`p` can be retargeted, `*p` cannot be written).
- An array of const elements is a const array: `const int32[3] a` and `int32 const*[2] a` cannot have
  their elements assigned.
- A `const` with no layer after it is an error, because it would apply to nothing:
  `int32* const p` -> `'const' after a type must come before a '*' or '[': write 'const T' for a const
  value, or 'T const*' for a const pointer`.
- A reference is never reseated, so `T const& r` is an error (`a reference cannot be const`); `const T&` is
  the reference to const.
- Top-level const in a template argument, a return type or a cast target has no effect (`Box<const int32>`
  is `Box<int32>`).

What is read-only (writing it is `cannot write through this location: <why>`): a const variable or
parameter (`'b' is const`), a location reached through a pointer or reference to const, a const field
(writable by its initializer and by a create, copy or delete of its own object), `this` in a const method
(05-objects 5.9), a lambda's copies of captured variables unless it is `mutable`. `&x` of a read-only `x`
is a pointer to const, and `take(x)` of one is an error.

The C declarator form `char b[4]` is a parse error. Function-pointer types use `funcptr<...>` (4.6).

Several declarators can share one statement: `int32 a = 1, b;` and `T first, second;` in a type body
both work.

## 4.3 Pointers

**Status:** **Implemented**

- `T*` is the only pointer type and it is **managed by the garbage collector**. There is no separate
  raw-pointer type at the language level. Memory that the GC must not manage comes from `raw_alloc` (see
  [../runtime/01-memory-and-gc.md](../runtime/01-memory-and-gc.md)).
- Address-of `&x`, dereference `*p`, member access `p->f`, indexing `p[i]`, pointer arithmetic with
  integer offsets and the difference of two pointers (an `int64` count of elements) work as in C.
- `.` and `->` are different operators, as in C++: `.` reaches a member of an object or a reference, `->` one
  through a pointer. `p.f` on a pointer is an error that names `->` (the editor offers the fix), and `o->f` on an
  object is an error that names `.`. `this` takes either spelling (`this.x`, `this->x`).
- `null` converts to any pointer and to any `funcptr`.
- `void*` is the universal pointer: any pointer converts to `void*` and back implicitly (including
  `T**` <-> `void**`, which COM out-parameters need).
- Other pointer conversions - in an assignment, a declaration, an argument or a return alike - are the same
  pointee, `null`, and a derived-to-base upcast. A pointer to const never becomes a pointer to mutable
  (`dropping const is not allowed`) except through `void*`; unrelated pointees are an error (`the pointees
  are unrelated`), and a base-to-derived conversion needs `as`.
- Pointers and integers are truthy in conditions (`if (p)`).
- `&r` of a reference (a local, a parameter or a field) is the address of its referent, as in C++.

## 4.4 References

**Status:** **Implemented**

`T&` and `const T&` behave like C++ references: parameters pass by address and read through
automatically, reference locals alias a location, functions may return references.

Rules enforced today:
- A non-const reference cannot bind to a const object: `cannot bind a non-const reference to a const object`.
- A reference cannot bind to a prvalue (a temporary): `cannot bind a reference to a prvalue`.
- A reference local binds only a location of its referent type, or a derived object through its base
  sub-object; a conversion would make a temporary it cannot hold (`cannot bind 'int32&' to a 'float': a
  reference names a location of its own type`).
- A function cannot return a reference to its own local or by-value parameter, or a part of one (`cannot
  return a reference to the local 'v': it ends when the function returns`).
- A non-const reference binds only an argument of its referent type (or a derived object, through its
  base sub-object). A `const T&` binds anything that converts to `T` implicitly (4.9): the value is
  converted into a temporary, destroyed at the end of the full expression (D-32). `const float&` accepts an
  `int32`; `float&` does not.
- Writes through a value returned by a function are rejected (`access chain is rooted in a function-call
  result that returns by value`), and so are writes through a const access chain (`access chain is rooted
  in a read-only location`).
- A **reference field** (`struct View { int32& target; }`) holds the address of its referent. It is bound where
  its object starts: by a braced entry (`View v = { x };`, `View{ x }`, `new View{ x }`), by its initializer
  (`int32& target = g_value;`), or by an assignment in its object's own create (`[operator("create")] void
  Init(int32& c) { count = c; }`, where C++ writes a member initializer). Every other use reads and writes the
  referent, and `&v.target` is the referent's address. A field nothing binds is an error (`the reference field
  'target' of 'View' is not bound`, `the create 'Init' of 'Holder' does not bind its reference field 'count'`), and
  so is a braced list or a temporary for one. A reference owns nothing: copying the object copies the reference
  (the copy names the same location), and destroying it leaves the referent alone.

## 4.5 Arrays

**Status:** **Implemented**

Array extents are compile-time constants. Any constant expression works: literals, `const` globals,
enum members, `sizeof`, template non-type parameters, every arithmetic, bitwise (`~` too), shift, comparison
and logical operator on them, `c ? a : b`, and a C-style cast or `static_cast` to a number or `bool`
(`int32[(int32)Size::Count] slots;`). Constant arithmetic has the run time's values: integers wrap at 64 bits
and a cast wraps to the target's width (`(uint8)300` is 44); a division by zero, a shift by a negative count or
by 64 or more, and a float cast out of the target's range are not constants and say so.

```c4
const int32 W = 4;
int32[W + 10] row;
float[sizeof(SzPoint) / 4] xs;
```

An extent is an integer constant of at least 1: `float[1e1]` (a floating-point value), `int32[0]` and
`int32[-1]` are errors that say so. A non-constant extent is an error that names it (`the array extent 'n +
1' is not a compile-time constant`). Arrays index with `a[i]` (no bounds checks), decay to pointers when
passed to pointer parameters, can be iterated with range-for, and support nested braced initialization
(`int32[3][2] g = { {1, 2, 3}, {4, 5, 6} };`: two rows of `int32[3]`) and designated initializers (see
[10-expressions](10-expressions.md)).

- `T[]` is not a type (D-33): `'int32[]' is not a type: an array needs an extent; use 'int32*' or
  'Core::Array<int32>'`. A run of unknown length is a pointer (with a count) or a Bedrock container.
- `packed T[N]` is not an array of `N` elements in memory but one SIMD vector value with lane-wise
  operators ([11 7](11-vectors-and-swizzling.md#7-packed-arrays-as-values)).
- A function may return a reference to an array: `int32[4]& Slots();` then `Slots()[i]` reads and writes the
  array itself, and `for (int32 v : Slots())` iterates it.
- `&r` of a reference local `r` is the address of what `r` refers to.

## 4.6 Function pointer types: `funcptr`, and `func`

**Status:** **Implemented**

```c4
funcptr<int32(int32, int32)> op = Add;   // function name decays to a pointer
funcptr<void()> callback = null;
int32 r = op(2, 3);                       // indirect call
```

`funcptr<Ret(Params...)>` replaces the C declarator syntax (AGENTS.md item 4). Values and calls are in
[07-functions-and-lambdas](07-functions-and-lambdas.md#75-function-pointers).

`funcptr<...>` is an ordinary type and takes every modifier: `funcptr<int32(int32)>[2]` is a callback
table, `funcptr<...>*` a pointer to one.

`func<Ret(Params...)>` is the callable value type: any function, funcptr, lambda or object with a call
operator of that signature, as a 16-byte value ([07-functions-and-lambdas](07-functions-and-lambdas.md#711-funcra)).
`func` is a type former only before `<`, so it remains usable as a name.

## 4.7 Type aliases: `alias`

**Status:** **Implemented**

```c4
alias Index = uint32;
template<T> alias Ptr = T*;
template<T> alias Vec8 = T[8];
alias FVector2 = TVector2<float>;
```

- `alias Name = Type;` at file scope, inside a type body, and inside a function body (block scoped,
  shadowable in a nested block).
- Alias templates are supported (see [08-templates](08-templates.md)).
- Aliases are transparent: an alias and its target are the same type for overloading and conversion.
- As a C++ typedef may, an alias may be declared again in its scope when it names the same type (`alias Size =
  uint64;` twice; the bindings of two C headers in one file both declare `size_t`). Naming a different type is an
  error: `redefinition of alias 'Size' as a different type: 'int64', was 'int32'`; so is an alias with the name of
  another type.
- There is no `typedef` or `using X = ...`.

## 4.8 Enumerations

**Status:** **Implemented**

```c4
enum Color { Red, Green, Blue };                     // members injected into the enclosing scope
enum class Traffic { Stop, Go, Caution };            // scoped: Traffic::Stop
enum class Priority : uint8 { Low = 1, High = 9 };   // explicit underlying type
```

- `enum` members are usable bare or qualified (`Red`, `Color::Red`); `enum class` members must be
  qualified (`Stop` alone is `no variable named Stop`).
- Two scoped enums may reuse a member name.
- Member initializers are constant expressions (`-1`, `0x1F`, `(A + B)`) and may name the members before them
  by their bare names (`enum class Step { A = 1, B = A + 1 }`). An enum whose member does not fold says which
  member and why (`unresolved enum 'E': member 'B': ...`).
- The underlying type (default `int32`) sets the size, signedness and range: a member value outside it is an
  error (`the enum member 'Big::Huge' is 9999999999, outside the range of its underlying type 'int32'`), and an
  enum on an unsigned type converts and compares unsigned (`(int32)Byte::Top` is 255 for `Top = 255` on
  `uint8`).
- `switch` works on scoped enums with qualified labels (`case Color::Red:`).
- An integer of the underlying type converts implicitly to an enum parameter. **Open question:** C++
  requires an explicit cast for `enum class`; decide whether C4 keeps this convenience.
- `enum struct` is accepted as a synonym for `enum class`.

## 4.9 Implicit conversions

**Status:** **Implemented**

Every place a value becomes a typed location uses the same rules (D-32): an initialization, an argument
(and a default argument), an assignment, a return, a braced element and a thrown value.

| From | To | Allowed implicitly | Overload cost |
|---|---|---|---|
| exact type (aliases unwrapped) | same | yes | 0 |
| any numeric | numeric of the same category (int -> int, float -> float) | yes, including narrowing | 1 |
| any numeric | numeric of the other category (int <-> float) | yes | 2 |
| a number or a pointer | `bool` | yes: non-zero (non-null) is `true` | 1 |
| integer of the underlying type | plain `enum` (and a plain enum to any number) | yes | 1 |
| an integer | `enum class` (and an enum class to a number) | **no**: `'Mode' is an enum class: ... only with a cast` | - |
| any pointer / `funcptr` | `void*` (and `void*` -> any pointer) | yes | 1 |
| `T*` | `const T*` | yes | 1 |
| `const T*` | `T*` | **no** (`dropping const is not allowed`) | - |
| `Derived*` | `Base*` | yes (adjusts for the base sub-object) | 1 |
| `Base*` | `Derived*` | **no** (`Cannot assign value of type 'Base*' to 'Mid*'`) - use a cast | - |
| `Derived` (by value) | `Base` | yes: a copy of the base sub-object (slicing) | 1 |
| `null`, the integer constant `0` | any pointer / `funcptr` | yes | - |
| any other integer | a pointer | **no**: `an integer is not a pointer: write null for no address, or cast` | - |
| a pointer | a number other than `bool` | **no**: `a pointer is not a number: cast with (int64) to read its address` (an integer: a floating-point target is told `(uint64)`) | - |
| a number, a pointer or another object | an object | **no**: `a 'int32' is not a 'Wrap': construct one with Wrap(...)` | - |
| an object | a number | **no**, even with a `cast` operator: `convert it with (float)x` | - |
| an array | a pointer to its element | yes (decay) | 1 |
| braced list `{...}` | `__init_list<T>` (or a pointer to one) | yes | 0 |
| anything converting to `T` | `const T&` | yes, through a temporary | that conversion's |

Arithmetic applies the usual C integer promotions and arithmetic conversions. User conversions are explicit
(D-32): a create runs only when written (`Wrap(5)`, `Wrap w(5)`), and a `cast` operator only through a cast.

Overload resolution sums the per-argument costs and picks the cheapest viable candidate. Equal best
costs are an error (`ambiguous call - equally good overloads: A and B`); no viable candidate is `no
matching overload of F`. See [07-functions-and-lambdas](07-functions-and-lambdas.md#73-overloading).

Narrowing stays implicit, as in C++, but warns (D-32): an initialization, assignment, argument or return
that converts to a narrower integer, a floating-point value to an integer, or a `double` to a `float` says
`the conversion from 'int64' to 'int16' may change the value: write (int16)x if that is intended`. A value
that folds to a constant that fits the target does not warn (`int8 small = 100;`, `float f = 0.1;`,
`uint8 m = 1 << 3;`); `(T)x` says the conversion is meant.

User-defined conversions use `[operator("cast")]` (see
[06-lifecycle-and-operators](06-lifecycle-and-operators.md#68-conversion-operator-cast)).

## 4.10 Initialization

**Status:** **Implemented**

- **Every variable is zero-initialized** before any constructor or initializer runs: locals, globals,
  fields and `new` allocations. There is no uninitialized memory in C4 code.
- `T v = expr;` copy-initializes; `T v(args);` direct-initializes, an object through its create and any other
  type from its one argument (`int32 x(5);` is `int32 x = 5;`; two arguments are an error: `'x' is not an object:
  it starts from one value, not 2`); `T v = {a, b};` aggregate-initializes (nested braces for nested
  aggregates); `T v = {};` is explicit zero.
- Direct initialization works for globals, static members and static locals as for locals. Outside a function
  body, `T v(a, b);` whose arguments are all bare names reads as a function declaration (`a` and `b` can be
  parameter types, as in `int32 Add(int32, int32);`); write `T v = T(a, b);` for such a variable.
- Global initializers that fold to constants become static data; others run once at startup in
  declaration order. That includes a `const` global whose initializer does not fold, such as a `const`
  object built with constructor calls ([01 1.7](01-program-model.md#17-order-independence-and-constants)). Static locals start the first time control reaches them. Both are destroyed at exit
  ([06-lifecycle-and-operators](06-lifecycle-and-operators.md#612-static-storage)).
- Object construction (`[operator("create")]`) is described in
  [06-lifecycle-and-operators](06-lifecycle-and-operators.md).

```c4
struct P { int32 a; float b; uint16 c; };
P p;            // all fields 0
P q = {};       // all fields 0
P r = {1, 2.0f, 3};
```

## 4.11 `__init_list<T>`

**Status:** **Implemented**

`__init_list<T>` is a built-in alias template that mirrors C++ `std::initializer_list<T>`: a 16-byte
`{ const T* data, uint64 size }` view over a braced list built by the caller (members `data` at offset 0
and `size` at offset 8).

```c4
int32 SumList(__init_list<int32> list) {
	int32 s = 0;
	for (int32 i : list) { s = s + i; }
	return s;
}

__init_list<int32> l = {10, 20, 30};
// l.size == 3, l[0] == 10, l.data is an int32*
```

It is what makes `new Core::Array<int32>{1, 2, 3}` work (a `create` overload taking `__init_list<T>`), and it
maps to `std::initializer_list` across C++ interop. Bedrock gives it a public name, `Core::InitList<T>`
(D-53). Each entry of the braced list initializes one element; a braced entry builds it
(`__init_list<Pair> l = { { 1, 2 }, { 3, 4 } };`).

## 4.12 What C4 does not have

| Feature | Status |
|---|---|
| `auto` / type inference | Not planned in current design (**Open question**) |
| Built-in string type | Never - by design; use `char*` or Bedrock strings |
| Nullable `T?`, `tuple<...>` | Not planned (old-doc ideas; multiple return values replace tuples, see [07 - Functions](07-functions-and-lambdas.md#79-multiple-return-values)) |
| `typedef`, `using` alias | Replaced by `alias` |
| Bit-fields | Not planned yet |

## 4.13 Storage modifiers: `volatile` and `thread_local`

**Status:** **Implemented** (D-29)

```c4
volatile int32 g_ticks = 0;         // every load and store of it is volatile
thread_local int32 t_depth = 0;     // each thread has its own copy
struct Port { public volatile int32 status; }

int32 NextId() {
	thread_local int32 next = 0;    // a thread_local local has static storage, as in C++
	next = next + 1;
	return next;
}
```

- `volatile` applies to a variable or a field (locals, globals, static locals, static members). Every load
  and store of it, or of any part of a volatile object, is a volatile operation the optimizer keeps and
  does not reorder with other volatile operations. It is a storage modifier, not part of the type: `&x`
  is a plain `int32*`, and loads through that pointer are not volatile.
- `thread_local` applies to a global, a static member or a local (which then has static storage). Each
  thread has its own copy. It may hold any type, managed pointers included: the collector scans every
  thread's copy (D-48).
- A copy that starts as a constant (06 6.12) holds it from the thread's first instruction. Any other start (an
  initializer computed at run time, a create, field initializers) runs on each thread: a thread_local global or
  static member starts when the thread attaches to the runtime, in declaration order, and a thread_local local
  starts on its thread's first pass, behind a guard only that thread reads (no lock). A start that throws leaves
  that thread's copy unstarted; a start that reaches its own declaration again stops the program.
- The main thread attaches after the globals start; a thread `thread_start` started attaches before its entry
  runs; any other thread (one a C library or the OS started) calls `thread_attach()` before it runs C4 code.
- A thread's copies that need destruction are destroyed when it ends, the last started first; the main thread's
  at exit, before the globals started earlier. An exception escaping such a delete stops the program.
- A field cannot be thread_local (`field 'n' cannot be thread_local`); a static member can.
- A thread_local variable is not in the reflection registry: it has no single address.

## 4.14 Strict aliasing

**Status:** **Implemented** (D-64)

Memory holding a value is read and written only through that value's type, as in C++. The optimizer relies on
it: a loop that stores floats through one pointer keeps the pointers it read from other memory in registers.

```c4
float f = 1.5f;
uint32 bits = reinterpret_cast<uint32>(f);    // the float's bits: a number to a number of the same size
uint8* bytes = (uint8*)&f;                    // a byte type reads any memory
union Pun { float f; uint32 u; }
Pun p; p.f = 1.5f; uint32 same = p.u;         // punning through a union is allowed
uint32 wrong = *(uint32*)&f;                  // undefined: a float read as an int32
```

An access is allowed when it is:
- of the value's own type, where all integers of one size are one type (`int32`, `uint32`, `snorm32`, `char32`
  and an enum whose underlying type is one of them), an alias is its target, an array is its elements and a
  `packed T[N]` is its `T` lanes;
- of a byte type, `int8`, `uint8`, `char8`, `snorm8` or `unorm8`, which reads and writes any memory;
- through a union member (a named union's or an anonymous union's, or an element of an array member), which
  reads the bytes any other member wrote;
- of an object containing the value: a copy of an object reads and writes every field.

Pointers, references, `funcptr` and `func` values are all one type. Anything else, such as `*(uint32*)&f` above,
is undefined behavior: the optimizer may reorder or drop it. Read a value's bits with `reinterpret_cast`, a union,
`memcpy` or a byte pointer instead. A `packed T[N]` is its lanes, so a `packed int32[4]` does not read a
`float` array: load it with `simd_load` from a `float*` and `reinterpret_cast` the vector (clang lets its vector
types read anything).

`-fno-strict-aliasing` turns the rule off for a program: every access may then touch any memory. Scripts always
follow the rule.
