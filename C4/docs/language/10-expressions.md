# 10 - Expressions

C4 expressions are C++ expressions. The differences are: operators on user types come from
`[operator(...)]` annotations ([06](06-lifecycle-and-operators.md)), swizzle runs on vector
types ([11](11-vectors-and-swizzling.md)), C# style `is`/`as`, the power operator `**`, and
initializer maps. Precedence and associativity are C++'s.

See also: [03 - Lexical structure](03-lexical-structure.md) (literal forms),
[04 - Types](04-types.md) (conversions), [14 - Builtins](14-builtins.md).

---

## 1. Precedence (normative)

**Status:** **Implemented**

Highest binds tightest. This is C++'s table with two C4 additions: `**` and `is`/`as`.

| Level | Operators | Associativity |
|---|---|---|
| 1 | `::` | left |
| 2 | postfix: `a()` `a[]` `.` `->` `a++` `a--` `T{...}` | left |
| 3 | `**` (power, C4 addition) | right |
| 4 | prefix: `++a` `--a` `+a` `-a` `!` `~` `*a` `&a` `(T)a` `sizeof` `new` `delete` | right |
| 5 | `*` `/` `%` | left |
| 6 | `+` `-` | left |
| 7 | `<<` `>>` | left |
| 8 | `<` `<=` `>` `>=` `is` `as` (type tests sit with the relational operators, as in C#) | left |
| 9 | `==` `!=` | left |
| 10 | `&` | left |
| 11 | `^` | left |
| 12 | `\|` | left |
| 13 | `&&` | left |
| 14 | `\|\|` | left |
| 15 | `?:`, `=`, `+=` `-=` `*=` `/=` `%=` `<<=` `>>=` `&=` `^=` `\|=` | right |

- `**` binds tighter than a prefix operator on its left and accepts one on its right (Python's rule):
  `-x ** 2` is `-(x ** 2)` and `2 ** -1` is `2 ** (-1)`. A cast is a prefix operator: `(float)x ** 2`
  is `(float)(x ** 2)`.
- An assignment takes the whole expression on its right: `x = a && b` is `x = (a && b)` and
  `x = c ? 1 : 2` is `x = (c ? 1 : 2)`.
- In `c ? a : b` the middle operand is a whole expression and the last one is an assignment
  expression, as in C++: `c ? x = 1 : y` is `c ? (x = 1) : y`, `c ? a : b = 2` is `c ? a : (b = 2)`.
- `is` and `as` read a type, then the relational level continues: `p as Node* == q` is
  `(p as Node*) == q`, `a && x is Node` is `a && (x is Node)`.
- An operator is a run of adjacent characters ([03](03-lexical-structure.md)): `a < = b` is a syntax
  error, not `a <= b`, and `*p * *q` stays three operators.

---

## 2. Primary expressions

**Status:** **Implemented**

- Literals: integers, floats, chars, strings (`char*`), `true`/`false`, `null`/`nullptr`
  ([03](03-lexical-structure.md)).
- Identifiers, `this` (a pointer to the receiver inside methods and extension methods).
- Parenthesized expressions.
- Qualified names: `Ns::f()`, `Type::StaticMethod()`, `Enum::Member`, `Outer::Inner`,
  `Tpl<int32>::Static()`.
- Function names used as values decay to `funcptr` ([07](07-functions-and-lambdas.md)).
- Lambdas `[](int32 x) -> int32 { ... }` ([07](07-functions-and-lambdas.md)).

## 3. Postfix expressions

**Status:** **Implemented**

| Form | Notes |
|---|---|
| `f(a, b)` | calls; overloads resolved by argument types; default arguments fill trailing parameters. A trailing comma `f(a, b,)` is accepted (needed when a macro expands an empty `__VA_ARGS__`). |
| `F<int32>(x)` | written template arguments; `F(x)` deduces them ([08](08-templates.md#5-argument-deduction)) |
| `a[i]` | array/pointer indexing, or `[operator("index")]` / `"index_assign"` on objects; a constant index outside a fixed-size array is an error (`the index 4 is outside the 'int32[4]' (0 to 3)`) |
| `obj.m`, `ptr->m` | member access: `.` on an object or a reference, `->` through a pointer; the other spelling is an error naming the right one. `this` takes either ([04](04-types.md)) |
| `obj.operator_name(...)`, `ptr->operator_name(...)` | call an operator generically by its canonical name ([06](06-lifecycle-and-operators.md)) |
| `v.xyz` | swizzle run on a vector type ([11](11-vectors-and-swizzling.md)) |
| `a + b`, `a < b` on `packed T[N]` | lane by lane, a scalar broadcast to every lane; a comparison gives a `packed bool[N]` mask ([11 7](11-vectors-and-swizzling.md#7-packed-arrays-as-values)) |
| `x++`, `x--` | yields the OLD value, then updates |
| `T{a, b}` | composite literal: a temporary of type `T` (also `Tpl<int32>{..}`, `Ns::Q<float>{..}`) |
| `T(a, b)` | constructs a temporary through `[operator("create")]` |

A call that returns `T&` (a function, an operator method or a funcptr) names the referent: it is read,
assigned through and bound to a reference like any location (`*it = 5;`, `int32& r = *it;`).

Writing through a member of a by-value call result is an error
(`access chain is rooted in a function-call result that returns by value`), as is writing
through a const access chain.

## 4. Unary expressions

**Status:** **Implemented** unless noted

| Form | Notes |
|---|---|
| `-x`, `+x`, `!x`, `~x` | numeric; on objects dispatch `negate`, `unary_plus`, `logical_not`, `bitwise_not` |
| `++x`, `--x` | yields the new value; on objects `increment`/`decrement` (one annotation serves prefix and postfix) |
| `&x` | address-of; `[operator("address_of")]` on objects |
| `*p` | dereference; `[operator("dereference")]` on objects |
| `(T)x` | C-style cast; on objects with a `[operator("cast")]` method, calls it |
| `dynamic_cast<T*>(p)` | checked cast, the same as `p as T*` (section 8) |
| `static_cast<T>(x)` | numbers (enums, bools, characters) to numbers; pointers between related objects (no run-time check), to and from `void*`; an object to one of its bases. Never removes const (D-12) |
| `reinterpret_cast<T>(x)` | a pointer to any pointer, and between a pointer and an integer; never removes const. Between numbers of the same size (not `bool` or an enum) it keeps the bits: `reinterpret_cast<uint32>(1.0f)` is `0x3F800000` ([04 4.14](04-types.md#414-strict-aliasing)) |
| `const_cast<T>(x)` | only adds or removes const, at any pointer level |
| `sizeof(T)`, `sizeof(expr)`, `sizeof expr` | the byte size of a type, or of an expression's type, as an `int64` compile-time constant (section 4.1) |
| `alignof(T)`, `alignof(expr)` | the alignment, an `int64` constant |
| `offsetof(T, field)`, `offsetof(T, a.b)` | the byte offset of a (nested) field, an `int64` constant |
| `__uuidof(T)` | pointer to the `_GUID` from `[uuid("...")]` ([13](13-interop.md)); error if `T` has none |
| `new T`, `new T(args)`, `new T{args}` | heap allocation (GC managed), see section 9 |
| `delete p` | runs `[operator("delete")]` then frees |

Each named cast rejects what it does not allow, naming the cast that would (`static_cast cannot convert 'A*'
to 'B*': the types are unrelated (reinterpret_cast reinterprets the bits)`); a C-style `(T)x` allows what any of
them allows: numbers and enums, any pointer to any pointer, a pointer and an integer, a derived object to its
base, and anything to `void` (discarding it). A pointer cast to `bool` is whether it is non-null. An object becomes anything else only through its
`[operator("cast")]` method (`a cast cannot convert 'Point' to 'int32': an object converts through its
[operator("cast")] method`), and a number is never cast to an object (`construct one with Point(...)`).
A cast to a reference, `static_cast<Base&>(d)` or `(Base&)d`, names the same location seen as that type (a related
object's sub-object, or the same bytes for `reinterpret_cast`); its operand must be a location, not a temporary.

A name in parentheses followed by `+`, `-`, `*`, `**`, `&` or `&&` is that operator's left operand, not a cast
(C#'s rule): `(v) * (v)` multiplies, so the macro body `((x) * (x))` works on any `x`. A built-in type name, a
type parameter of the template being declared, or a type with more to it (`(int32)-x`, `(T)-1` in a template,
`(T*)&x`, `(const T)*p`) still casts; to cast such an operand to a named type, parenthesize it: `(Mode)(-1)`. Writing `(Mode)-1` is an error that says so: `'(Mode) -' is not a cast: a name in
parentheses before '-' is its left operand; write '(Mode)(-x)' to cast`.

Pointer arithmetic follows C: `p + n`, `p - n`, `p[i]`, `p++`, pointer comparison, and `p - q`, the
number of elements between two pointers to one type (an `int64`; `const` on either side does not
matter). `void*` has no element size, so a difference of two `void*` is an error: cast them to `char*`.

### 4.1 sizeof, alignof, offsetof

```c4
struct Outer { public int32 id; public Inner inner; public int16[3] codes; }
int32[5] g_Table;
const int32 kCount = sizeof(g_Table) / sizeof(g_Table[0]);   // 5, in a constant
int64 a = sizeof(Outer);            // with padding
int64 b = sizeof o.inner;           // no parentheses for an expression
int64 c = alignof(o.codes);
int64 d = offsetof(Outer, inner.value);
```

- The operand of `sizeof` / `alignof` is a type or an expression. An expression is never evaluated:
  `sizeof(Tick())` does not call `Tick`. `sizeof x * 2` is `(sizeof x) * 2`.
- `sizeof(x)` with a name means the type `x` when one exists, else the value `x`.
- A reference measures what it refers to. `sizeof(void)` and an incomplete type are errors.
- `offsetof` names a non-static field, through nested fields with `.`: an unknown or static field is an
  error.
- In a constant (an array extent, a `const` initializer) an expression operand is a variable, an element
  (`a[0]`), a field (`s.x`, `p->x`), a dereference, a literal or a cast; inside a function body it may be any
  expression.

## 5. Binary expressions

**Status:** **Implemented**

- Arithmetic `+ - * / % **`, shifts `<< >>`, bitwise `& | ^`, comparisons, logical `&& ||`
  (short-circuit) - on primitives built in, on objects through operator annotations
  ([06](06-lifecycle-and-operators.md)). The operator is a method of the left operand's type; the
  operands never swap (D-25).
- The built-in operators take: numbers for `+ - * / % **` (and a pointer with an integer for `p + n`,
  `n + p`, `p - n`, or two pointers to one type for `p - q`), integers for `& | ^ << >> ~`, numbers or two pointers for comparisons. Anything else
  is an error that names both types (`operator '&' needs integer operands, not 'float' and 'float'`).
- An operand written as a constant that already makes the operation undefined is an error: an integer `/`
  or `%` by `0` (`division by zero: the right operand of '/' is the constant 0`) and a shift count outside 0
  to the operand's width less one (`the shift count 32 is outside 0 to 31 for a 'int32'`).
- `%` on floating-point operands is the C `fmod` remainder: its sign is the dividend's (`-5.5f % 2.0f` is
  `-1.5f`).
- `++` / `--` step a number by 1 and a pointer by one element; a `bool` has no successor.
- Usual arithmetic conversions and integer promotions apply as in C.
- Assignment `=` on objects runs the type's `[operator("=")]` when declared; otherwise a type with
  parts to copy or destroy gets a copy of the new value, its old value destroyed and the copy moved in,
  and any other type copies bytes ([06](06-lifecycle-and-operators.md#64-copy-move-and-assignment)).
- Compound assignment prefers `<op>_assign` and falls back to `a = a <op> b`; a pointer stays a pointer
  (`p -= q` makes a distance and `n += p` an address, so both are errors).
- `**` (power, D-26): on integers by repeated squaring with wrapping multiplication - a negative
  exponent gives 0, except that `1 ** n` is 1 and `-1 ** n` is 1 or -1 (`(-1) ** -3` is -1); on floats it is
  C's `pow`. Mixed operands convert first, as for `*`, and constants fold (`int32[2 ** 3]`). `**` is right
  associative and binds tighter than a prefix operator (section 1), so `(int64)3 ** 30` is
  `(int64)(3 ** 30)`. Note that `a**p` lexes as `**`, not `a * *p`; write `a * *p`.

## 6. Conditional expression

**Status:** **Implemented**

`c ? a : b` evaluates only the chosen branch. Both branches become one type: the type they share, the usual
arithmetic result of two numbers (an enum counts as its underlying integer), or the type one branch converts to
implicitly (a derived object or pointer to its base, `null` to the other branch's pointer or funcptr). An object
result is a new object, copied from a branch that names one. Anything else is an error (`the branches of '?:'
have no common type: 'int32*' and 'float*'`).

## 7. Type tests: `is` and `as`

**Status:** **Implemented** for pointers (reference targets: Planned)

```c4
Animal* a = new Dog();
if (a is Dog) { ... }            // the dynamic type is Dog or derives from / implements it
Dog* d = a as Dog*;              // the Dog object, or null when `a` is not a Dog
IB* b = d as IB*;                // a cross-cast between interfaces of one object
Dog* same = dynamic_cast<Dog*>(a);
```

- `x is T` is `true` when the dynamic type of `x` is `T`, derives from `T` or implements it. For a
  pointer to a polymorphic object it is a run-time check; for any other operand (an object value, a
  pointer to a type without virtual methods) it is decided by the static type.
- `p as T*` is a checked conversion of a pointer to an object: an upcast is a plain conversion; a
  downcast or a cross-cast reads the object's run-time type information (MSVC RTTI, the same tables C++
  uses) and yields the `T` sub-object or `null`. A `null` operand yields `null`, and `is` on it is `false`.
- A checked downcast needs a polymorphic operand: `cannot check the dynamic type of 'Plain': it has no
  virtual methods`. A cast between unrelated or non-object types is an error:
  `a checked cast needs pointers to objects`.
- `as` with a numeric target is an error that names the cast to use:
  `'as' converts pointers to objects; for a numeric conversion use a cast: (double)x`.

- `x as T&` and `dynamic_cast<T&>(x)` name the `T` that `x` is, as a location (writes go to the object). When `x`
  is not a `T` they throw the prelude's `BadCast`, whose `target` is the name of `T`
  ([14](14-builtins.md)). `x` is evaluated once; an upcast checks nothing. `x` must be a location (`a checked cast
  to a reference needs a location to refer to, not a temporary`), and a const `x` gives a `const T&`.

```c4
try {
	Square& square = shape as Square&;
	square.size = 2;
} catch (const BadCast& e) {
	printf("not a %s
", e.target);
}
```

## 8. Allocation: `new` and `delete`

**Status:** **Implemented** (single objects), **Planned** (arrays)

```c4
GCNode* a = new GCNode;          // zeroed; runs the 0-arg create if it has one
OverCtorB* b = new OverCtorB(10);// create overload chosen by arguments
Vec2* v = new Vec2{ 1.5f, -2.5f };// aggregate init when there is no matching create
Box<int32>* bx = new Box<int32>{4};// braced args bind a create taking those args or __init_list<T>
int32* n = new int32;            // primitives too
delete b;                        // runs [operator("delete")] once, then frees
```

- Every `T*` is a garbage-collected pointer; `delete` is optional and only makes reclamation
  immediate. The finalizer runs exactly once, whether by `delete` or by the collector
  ([runtime/01-memory-and-gc.md](../runtime/01-memory-and-gc.md)).
- `new T[n]` array allocation does not exist yet (**Planned**); containers use `raw_alloc` plus
  `gc_register_region` instead.

## 9. Braced initialization

**Status:** **Implemented**

```c4
Point p = { 1.0f, 2.0f };                     // aggregate, positional
Line l = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };  // nested aggregates
int32[3][3] m = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
return { a, b };                              // braced return value

int32[10] i = { [0] = 0, [6] = 1 };           // designated elements
int32[8] a = { 1, 2, [5] = 9, 7 };            // positional continues after a designator
int32[8] r = { [2...5] = 42 };                // inclusive range (also written [2..5])
int32[2][4] m = { [1...2] = { 7, 8 } };       // a range of rows
Point q = { y = 2.0f };                       // a field designator
Line k = Line{ to = { y = 1.0f } };           // designators in a composite literal
```

- Storage is zeroed before any initializer runs. A field no entry names starts from its initializer or
  its default construction (05-objects, 06-lifecycle-and-operators 6.2); an element no entry names is
  default-constructed (zero for a plain type).
- Elements start in index order. A range entry evaluates its value once, into its first element, and
  copies it into the rest; its element type must be copyable. An element named twice is an error:
  `element N of 'name' is initialized twice`.
- A field designator names the field it fills (or a packed lane, by any of its names); entries run in the
  order written, then the fields no entry names start in declaration order. Positional entries fill the
  instance fields in order: a static is never filled, a union fills its first view only (the others name the
  same bytes, as in C), and a packed region fills its lanes. A type with creates takes its braced arguments in
  order and rejects designators.
- A braced list initializes an array or object field of a composite literal or of `T(...)` the same way:
  `Row{ 1, { 2, 3, 4 } }`.
- A braced list passed to a parameter of type `__init_list<T>` builds an initializer list
  (`SumInts({1, 2, 3})`), which also maps to C++ `std::initializer_list` over interop. Each entry
  initializes one element as a variable of type `T` would; a braced entry builds the element
  (`SumPairs({ { 1, 2 }, { 3, 4 } })` for an `__init_list<Pair>`), as in C++.
- A braced argument also binds an object parameter, by value or `const T&`, that its entries build (an
  aggregate with at least as many slots, a create taking that many arguments, or a create taking an
  `__init_list<T>`), and an array parameter of at least as many elements, as in C++ (`Take({ 5, 6 })`), through a
  function pointer too (D-57). An `__init_list<T>` parameter is the better match; two object parameters the
  same braces build are ambiguous, and the error names both.
- On the right of `=`, a braced list builds a value of the target's type: `p = { 3, 4 };`, `cells = { 1, 2 };`
  (elements no entry names are zero), `p = { y = 1 };`. Anywhere else a braced list needs the type it
  builds (`the braced list '{ 1 }' has no type here: write the type it builds, T{...}, or initialize a
  variable`).

## 10. Not supported

| Form | Status |
|---|---|
| comma operator `a, b` | not an operator (D-40); a `for` step takes a comma-separated list ([09](09-statements-and-control-flow.md#2-conditionals-and-loops)) |
| `typeof`, `decltype` | **Open question** (no `auto` either) |
| `?.`, `??` null-conditional operators | **Open question** |
| call operator `[operator("()")]` | **Open question** - not in the operator table |
| `<<=`, `>>=` as user operators | **Planned** - parsed as assignments, no operator names |
| pointer-to-member | not planned |
