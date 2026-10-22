# 7. Functions, function pointers and lambdas

Functions in C4 are C++ functions: free functions, methods, static methods, overloads, default
arguments and C variadics. Function pointers use the `funcptr<Ret(Params)>` type; lambdas are closure
objects (with C++ captures) that convert to `funcptr` when they capture nothing and to the callable value
type `func<Ret(Params)>` always. Templates are covered in [08-templates](08-templates.md);
operator methods in [06-lifecycle-and-operators](06-lifecycle-and-operators.md).

---

## 7.1 Function declarations

**Status:** **Implemented**

```c4
void DoThing();
int32 Add(int32 a, int32 b);
float Lerp(float a, float b, float t) { return a + (b - a) * t; }
public static void Probe() { }
inline void Force(); // inline will force inline
```

General shape (in order): annotations, `template<...>`, access modifier, modifiers (`static`, `inline`,
`virtual`, `override`, `abstract`, `final`), return type, qualified name, parameter list, trailing
qualifiers (`const`, `override`, `final`, `noexcept`), `= 0`, `extends T`, then a body or `;`.

- `inline` puts the function's body in place of every call it can, at every optimization level (LLVM's
  `alwaysinline`): for a hot path too small to be worth a call, such as a container's capacity check. A function
  declared in its type and defined out of line is inline when either says so. Its address can still be taken, and
  a recursive or virtual call stays a call.
- The return type comes first, as in C++. There is no trailing-return form for named functions.
- Parameter names may be omitted in a declaration: `void F(int32);`.
- `return;` without a value in a non-`void` function is an error (`missing return value for non-void
  function`), and so is control that can reach the end of one (D-55): `control reaches the end of non-void
  function 'Sign': return a 'int32' on every path`, at its closing brace. A body ends on every path when each
  path returns, throws, or loops forever (`while (true)` with no `break`); a `switch` whose cases all
  return, with a `default`, ends too. Lambdas follow the same rule. `main` is the exception: reaching its end
  returns 0.
- Function-local functions (a function defined inside another function's body) are lowered as ordinary
  functions scoped to the block. They are not closures: they cannot see the enclosing function's locals.
  Covered by C4Testing HIR tests only; no end-to-end test yet.
- A trailing `const` makes a method const ([05-objects](05-objects.md#const-methods)); `noexcept` stops the
  program when an exception would leave the function (D-13). The trailing qualifiers combine
  (`int32 Get() const noexcept`). `abstract` and `final` are described in
  [05-objects](05-objects.md#abstract-and-final); `concurrent` is not a C4 modifier (an error).
- A parameter declared `const` (`void F(const int32 x)`) is read-only in the body. Like C++, that `const`
  belongs to the definition: a declaration and its definition may differ in it.

## 7.2 Declarations, definitions and out-of-line methods

**Status:** **Implemented**

Because of the program model ([01-program-model](01-program-model.md)), a function needs a declaration
in a header only so that *other files' bodies* can call it. Within one program, a free function may be
defined in any `.cf`.

```c4
// Shape.ch
struct Shape {
	float x;
	[operator("create")] void Init();
	virtual float GetArea();
};

// Shape.cf
include "Shape.ch"
void Shape::Init() { x = 0.0f; }
float Shape::GetArea() { return 0.0f; }
```

- Out-of-line methods use `Type::Method`; namespaced types use `Ns::Type::Method`. The definition must match
  a method declared in the type's body: its name, parameter and return types, and `const`
  (`the definition of 'Box::Set' matches no declaration in 'Box': it is void (float); 'Box' declares void
  (int32)`). A class template's definitions are checked before any instance exists where no argument could
  make them match (`T` against `float`, `const K&` against `K`), and when an instance is made otherwise.
- `Ns::F` defines the function `F` declared in `Ns`, exactly as if it were written inside `namespace Ns { }`.
- Parameter names in the definition may differ from the declaration.
- Template methods are defined out of line with a leading `template<T>` and without `<T>` on the owner:
  `template<T> public void Box::Push(T v) { ... }` (see [08-templates](08-templates.md)).
- The same signature defined twice is `Duplicate definition of 'f'`.

## 7.3 Overloading

**Status:** **Implemented**

Functions, methods, extension methods and constructors overload by parameter types and count. Resolution
sums a per-argument conversion cost (exact = 0, numeric same category = 1, numeric across categories = 2,
pointer to/from `void*` = 1, a plain enum from its underlying integer = 1, a derived object to a base = 1)
and picks the cheapest viable candidate. See the full table in [04-types](04-types.md#49-implicit-conversions).

- Ties are an error: `ambiguous call - equally good overloads: A and B`.
- No viable candidate: `no matching overload of F` (also used for wrong argument counts).
- A non-const reference parameter matches only its referent type (or a derived object); a `const T&`
  parameter matches anything that converts to `T`, at that conversion's cost.
- Each variadic argument costs 1, so an exact non-variadic overload beats a variadic one.

## 7.4 Default arguments

**Status:** **Implemented**

```c4
int32 g_Base = 100;
int32 Seed();

int32 DpAdd(int32 a, int32 b = 5) { return a + b; }
int32 DpThree(int32 a = 1, int32 b = 2, int32 c = 3) { return a + b + c; }
int32 DpExpr(int32 x = g_Base + 1, int64 n = sizeof(Pair) * 2, int32 s = Seed() * 3, int32 m = -1);

DpAdd(1);      // 6
DpThree(4);    // 9
```

- Trailing omitted arguments take their declared defaults. A default is any expression; it is evaluated
  again at every call that omits it, as in C++.
- A default's names are bound where the function is declared, not at the call: a caller's local or member
  named like a global the default uses does not change it, and a default cannot name another parameter
  (`int32 F(int32 a, int32 b = a)` is the error `no variable named a`) or `this`.
- Methods and creates may have defaults too. A create whose every parameter has a default is the type's
  default create ([06](06-lifecycle-and-operators.md#62-construction-create)).

## 7.5 Function pointers

**Status:** **Implemented**

```c4
int32 Add(int32 a, int32 b) { return a + b; }

funcptr<int32(int32, int32)> g_op = Add;        // global, initialized from a function

int32 ApplyOp(funcptr<int32(int32, int32)> op, int32 a, int32 b) {
	return op(a, b);                            // indirect call
}

funcptr<void()> cb = null;
if (cb != null) { cb(); }
```

- A function's name decays to a `funcptr` of matching signature, qualified by its namespace or not (`Add`,
  `Geo::Area`), and so does a static method's (`Type::Make`, or `Make` inside `Type`), checked for access as a call
  is; `&Add` is the same address. A function template's instance is named with its arguments (`Max<int32>`,
  `Geo::Min<float>`) where no comparison could stand: before `;`, `,`, `)`, `]`, `}`, `:`, `?`, `==`, `!=`, `&&`,
  `||`, `|` or `^` (C#'s rule), so `F(a < b, c > d)` still passes two comparisons. A bare template name works where a
  `funcptr` or `func` type says which instance it is. An instance method has no address without an object
  (`'S::Inst' is a method: it has no address without an object; pass a lambda that calls it on one`), and a name
  with several overloads is an error: a funcptr names one function.
- `funcptr` values can be compared with `null`, passed, returned, stored in fields and globals.
- A `funcptr` is an ordinary pointer at runtime (C ABI compatible), so it can be passed to C callbacks
  through interop (for example a Win32 window procedure).
- There are no pointers to members; pass an object pointer plus a free function instead.

## 7.6 Lambdas and closures

**Status:** **Implemented** (D-22)

```c4
funcptr<int32(int32)> twice = [](int32 x) -> int32 { return x * 2; };   // captures nothing: a funcptr
int32 base = 10;
func<int32(int32)> add = [base](int32 x) -> int32 { return x + base; }; // a copy of base
int32 total = 0;
func<void(int32)> sum = [&total](int32 x) { total += x; };            // total itself
[](int32 a, int32 b) -> int32 { return a * b; }(6, 7);                  // called where it is written
```

- Syntax: `[captures](params) mutable -> Ret { body }`. `-> Ret` is required when the lambda returns a value
  (it is never deduced); omit it for `void`. `mutable` is optional.
- **Captures** name what the body may use from the enclosing function:

| Capture | Meaning |
|---|---|
| `[]` | nothing |
| `[x]`, `[&x]` | `x` by value (a copy made when the lambda is created), by reference (`x` itself) |
| `[=]`, `[&]` | every enclosing local the body uses, by value / by reference; written first |
| `[=, &y]`, `[&, x]` | a default plus exceptions |
| `[n = expr]`, `[&r = lvalue]` | an init capture: a new name initialized from `expr` (once, when the lambda is created), or bound to a location |
| `[this]` | the enclosing object's address: `this` and members in the body are the enclosing object's (private members included) |
| `[*this]` | a copy of the enclosing object |

- Inside a method, `[=]` and `[&]` also capture `this` when the body uses `this` or a member.
- Using an enclosing local that is not captured is an error naming it; so is capturing a global (use it
  directly) or capturing `this` outside a member function. Parameters, the body's own locals, globals and
  functions need no capture. A static local of the enclosing function is reached like any local: through a
  capture.
- A by-value capture is read-only in the body unless the lambda is `mutable`; with `mutable`, changes persist
  between calls of the same closure and never reach the original variable. The copy is the lambda's own, so
  it is writable in a `mutable` lambda even when the variable it copies is const (C++ keeps it const). A
  by-reference capture of a const variable points at const.
- A by-reference capture refers to the variable itself: calling the lambda after the variable's scope has
  ended is undefined, as in C++.
- **Each lambda expression is a closure object** of its own unique type: an object whose fields are the
  captures, with an `[operator("call")]` method whose body is the lambda's. It is copied with its captures
  (and is not copyable if a by-value capture is not), and destroys its by-value captures when it ends.
- **Conversions:** a lambda that captures nothing converts to `funcptr<R(A)>` of its signature; any lambda
  converts to `func<R(A)>` of its signature (7.11). A capturing lambda cannot become a funcptr.
- **Templates** take the closure type itself: `template<F> void Each(F f)` deduces `F` as the lambda's type,
  and `f(x)` calls its call operator directly (no indirection, inlinable). The same template accepts a func, a
  funcptr or any object with a call operator.
- Nested lambdas capture from the enclosing lambda's scope (its parameters, locals and captures).

## 7.7 C variadic functions

**Status:** **Implemented**

```c4
int32 SumInts(int32 count, ...) {
	va_list ap;
	va_start(ap);
	int32 sum = 0;
	for (int32 i = 0; i < count; i++) {
		sum += va_arg(ap, int32);
	}
	va_end(ap);
	return sum;
}

int32 s = SumInts(3, 1, 2, 3);
```

- A trailing `...` parameter makes a function variadic; calls promote arguments with the C rules
  (`float` -> `double`, small integers -> `int32`).
- The arguments are read through a `va_list` (D-35): `va_start(ap)` opens them (only in a variadic
  function), `va_arg(ap, T)` reads the next one as a `T`, `va_copy(dst, src)` copies a list so its arguments
  can be read again, and `va_end(ap)` closes it ([14-builtins](14-builtins.md#8-variadic-argument-access)).
- `va_arg` reads the promoted type: `va_arg(ap, float)` is an error that says to read a `double`, and
  `bool` / `int8` / `int16` / `uint8` / `uint16` / `char8` / `char16` arrive as `int32`.
- `printf` is a builtin variadic; C variadic functions from interop headers bind with their `...`, and a
  C4 variadic function can be called from C (`assets/IsolatedTest/Interop/VarargsToC`).
- Variadic **templates** use packs (D-19, [08-templates](08-templates.md)), not `...` arguments.

## 7.8 Calls

**Status:** **Implemented**

- Arguments are evaluated and passed by value unless the parameter is a reference. An object passed by
  value from a variable is copied (its copy operator); a temporary moves into the parameter. The callee
  owns its by-value parameters and destroys them when it returns
  ([06-lifecycle-and-operators](06-lifecycle-and-operators.md#64-copy-move-and-assignment)).
- A trailing comma before `)` is allowed: `F(a, b,)` (needed for macro forwarding, see
  [02-preprocessor](02-preprocessor.md#22-stringize-paste-and-variadic-macros)).
- Methods are called with `.` or `->` (both work on pointers); static methods with `Type::F()`;
  template arguments are written (`F<int32>(x)`) or deduced from the arguments (`F(x)`,
  [08-templates](08-templates.md#5-argument-deduction)).
- Calls through `operator_<name>` reach operator methods by operator name
  ([06-lifecycle-and-operators](06-lifecycle-and-operators.md#69-custom-operators-and-generic-operator-calls)).

## 7.9 Multiple return values

**Status:** **Implemented** (D-34)

A function returns several values at once with a **return list** type, and a **destructuring** hands each
value to a variable. This replaces tuples, which C4 does not have.

```c4
[float, bool] Parse(int32 v) { return [(float)v * 0.5f, v > 0]; }

[float half, bool ok] = Parse(5);        // declare both
float a;
[a, bool neg] = Parse(-2);               // assign an existing variable, declare another
[int32 first, _, int32 third] = Three(); // `_` discards a value
[x, y] = [y, x];                         // swap: the right side is evaluated first
[p.x, items[i]] = Pair();                // any assignable location
```

- **Return lists.** `[T1, T2, ...]` with at least two types is written where a return type is: a function's
  or method's, a `funcptr<[T1, T2](A)>` or `func<...>` signature's, a lambda's `-> [T1, T2]`. A pack may
  expand in it (`template<...Ts> [Ts...] F(Ts... args)`). An element is a value (no reference, no
  `void`).
- **The type.** One anonymous struct per list of element types, laid out like a C struct, and returned by the
  C ABI rules for a struct of that layout (in registers when small, through a hidden pointer otherwise).
  `[int32, bool]` written twice is one type. Return lists do not take part in overloading (a function's
  return type never does). A template parameter deduced from one is that type, and a template may return
  it on.
- **Value lists.** `[e0, e1, ...]` gives the values of a return list: in a `return` of a function returning
  one (each value converts to its element, as a braced entry does; the count must match), and on the right of
  a destructuring. A pack may expand in it (`return [args...];`). Anywhere else it is an error.
- **Destructuring** is a statement `[e0, e1, ...] = value;` whose right side is a return list (a call, or a
  value list) and whose elements, one per value, are:
  - `Type name` - a new variable of the enclosing block, starting from the value (it converts as a
    declaration's initializer does; `const` is allowed; a reference is not);
  - an assignable location (`a`, `p.x`, `items[i]`) - assigned the value;
  - `_` - the value is discarded (destroyed).
- **Order and cost.** The right side is evaluated once, completely, before any element is touched; then the
  elements are handled left to right. Each value is moved out of the list (an object relocates, it is never
  copied); a value that is discarded, or left when an element fails, is destroyed when the statement ends.
  Destructuring a list held by a variable (in a template) copies it first.

## 7.10 Differences from C++

| C++ | C4 |
|---|---|
| `int (*f)(int)` / `std::function` | `funcptr<int32(int32)>` / `func<int32(int32)>` (a func copy shares its closure) |
| `[](int x) { return x; }` deduces the return type | `-> Ret` is required for value-returning lambdas |
| `auto` lambda variables | A lambda is stored in a `func<...>` (or a `funcptr<...>` when it captures nothing), or passed to a template |
| Default arguments: any expression | The same; a default is evaluated at each call, with names bound where it is declared (7.4) |
| `va_list` + `va_start(ap, last)` | `va_list` + `va_start(ap)`: no last named parameter (7.7) |
| `std::tuple` / structured bindings | A return list `[T1, T2] F()` and a destructuring `[a, T2 b] = F();` (section 7.9) |
| Templates must be in headers | Function bodies (template or not) may live in `.cf` |

## 7.11 `func<R(A)>`

**Status:** **Implemented** (D-22)

```c4
func<int32(int32)> f = null;
f = Double;                                 // a function
f = [k](int32 x) -> int32 { return x + k; }; // a closure
if (f != null) f(3);
struct Handler { public func<void(int32)> callback; }
func<int32(int32)>[4] table;                // in fields, arrays, parameters and returns like any value
```

- A func holds any callable of its signature: a closure, a function, a `funcptr`, or any object with a call
  operator of that signature. It is 16 bytes, `{ code, environment }`, copies freely, compares with `null`
  (`f == null`, `!f`, `if (f)`, either side of `&&` and `||`, a `?:` condition) and is called like a function. As
  in C++, the test is contextual: `bool b = f;` is an error.
- A func made from a closure that captures values holds a garbage-collected copy of the closure, so the
  func can outlive the function that made it; copying the func shares that copy (a `mutable` lambda's state
  is shared by all copies of the func). A closure whose only capture is one pointer (`[this]`, `[&x]`) needs
  no allocation: the func holds the pointer.
- Calling a null func is undefined, like calling a null funcptr.
- A signature mismatch is an error: `the lambda cannot become 'func<int32(int32)>': no call operator of that
  signature`.

## 7.12 The call operator

**Status:** **Implemented** (D-23)

`[operator("call")]` (token `"()"`) makes `x(args)` on an object call the method; it overloads like any
method and is reached generically as `x.operator_call(args)`. A value of an object type with no call operator
cannot be called: `'Plain' cannot be called: it has no [operator("call")] method`.

```c4
struct Multiplier {
	public int32 factor;
	[operator("call")] int32 Apply(int32 x) { return x * factor; }
}
Multiplier m; m.factor = 3;
int32 r = m(5);   // 15
```
