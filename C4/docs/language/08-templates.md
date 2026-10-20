# 08 - Templates

C4 templates are C++-style generics: a declaration parameterized by types and compile-time
values, instantiated (monomorphized) for each distinct argument list. The surface syntax is
simpler than C++ (`template<T>` needs no `typename`), and because of the program model a
template's body does not have to live in a header.

See also: [07 - Functions and lambdas](07-functions-and-lambdas.md),
[05 - Objects](05-objects.md), [04 - Types](04-types.md) (aliases),
[compiler/02-program-model.md](../compiler/02-program-model.md) (how instantiation works inside
the compiler).

---

## 1. Template parameter lists

**Status:** **Implemented** (type and value parameters, defaults, variadic packs - section 6)

```c4
template<T>                 // one type parameter
template<A, B>              // several type parameters
template<class T>           // `class` / `typename` prefixes are accepted and mean the same thing
template<T, uint32 I>       // mixed: type + value
template<int32 N, T>        // a value parameter may come first
template<float F>           // value parameters may be any scalar type
template<T = int32, uint32 N = 4>   // defaults (D-18)
template<T, U = T*>         // a default may name an earlier parameter
template<...Ts>             // a type pack (section 6)
template<T, ...int32 Ns>    // a value pack after a type parameter
```

- A bare name (`T`) is a type parameter. `class T` and `typename T` are the same thing.
- A parameter written `Type Name` is a value parameter. Its argument is a constant expression
  (`Arr<3>`, `Arr<N * 2>`, `Arr<kSize>`, `Arr<sizeof(T)>`); inside the template it behaves like a named
  constant (usable in expressions, array sizes and `sizeof`). A comparison in an argument needs
  parentheses (`Flag<(A > B)>`), as in C++.
- Arguments bind to parameters **by position**, whatever the mix of kinds. A type where a value parameter
  is (or the reverse) is an error naming the parameter: `template parameter 'N' of 'Arr' takes a constant
  value`. More arguments than parameters: `template 'Box' takes 1 argument(s), got 2`.
- Trailing arguments may be left out when their parameters have defaults; `Buf<>` uses every default. A
  parameter that gets no argument at all is `no argument for template parameter 'T' of 'Make': write it,
  or give it a default`.

```c4
template<T, uint32 I> T GetI() { return (T)I; }
template<int32 N, T> T Scale(T v) { return v * (T)N; }
template<T = int32, int32 N = 4> struct Buf { public T[N] data; }

int32 main() {
	if (GetI<int32, 42>() != 42) return 1;
	if (Scale<3, int32>(2) != 6) return 2;
	Buf<> a;            // Buf<int32, 4>
	Buf<float> b;       // Buf<float, 4>
	return 0;
}
```

Constraints / concepts: **Open question** (no design yet).

---

## 2. What can be a template

**Status:** **Implemented**

| Kind | Example |
|---|---|
| Class / struct / interface | `template<T> struct Box { public T value; }` |
| Free function | `template<T> T Identity(T v) { return v; }` |
| Extension method | `template<T> T Sum() extends Pair<T> { return first + second; }` |
| Alias | `template<T> alias Ptr = T*;` |
| Out-of-line method of a template | `template<T> float Vec<T>::Len() { ... }` |
| Member function template | `struct P { template<U> U Scale(U u) { ... } }` (section 2.5) |

### 2.1 Object templates

```c4
template<T> struct TplBox {
	T value;
	T extra;

	[operator("create")] public void Init(T v, T e) {
		this.value = v;
		this.extra = e;
	}

	public T Get() { return this.value; }
}

template<int32 N> struct FixedArr {
	public int32[N] x;
	public int32 Count() { return N; }
}

void Use() {
	TplBox<int32> b = TplBox<int32>(40, 2);
	FixedArr<5> a;           // sizeof(FixedArr<5>) == 20
	a.x[4] = 1;
}
```

- Methods may be defined inline in the template body or out of line (section 3).
- Static members work per instantiation: `TplPair<int32, float>::Sum(1, 2)`.
- A member alias may name a parameter: `alias Type = T; Type val;`.
- Templates may be declared inside namespaces and used qualified (`Geometry::Cube<int32>`) or
  through an alias (`alias ICube = Cube<int32>;`).
- A template instance may be a base class (`struct Circle : public Shape<float>`), and operators declared
  on the template base are inherited.
- Inside the body the template's bare name is the instance being built (C++'s injected class name):
  `public T Sum(const Amount& o)` in `Amount<T>` takes an `Amount<T>`, and `Amount<float>` written in the body
  still names that other instance.
- A struct, class, enum or class template declared in the body belongs to each instance and may name the
  parameters: `template<T> struct Outer { struct Inner { T t; } enum class Mode { A, B } }` gives
  `Outer<float>::Inner`, `Outer<float>::Mode::B` and `Outer<int32>::Inner{ 7 }`, each instance's its own type
  (reflected as `Outer<float>::Inner`).

### 2.2 Function templates

```c4
template<T> T TplIdentity(T v) { return v; }
template<A, B> A TplAdd(A a, B b) { return a + b; }

int32 a = TplIdentity(7);                       // T deduced (section 5)
uint64 c = TplAdd<uint64, uint64>(1000000, 500000);
namespace Math { template<T> T Twice(T v) { return v + v; } }
float d = Math::Twice(2.5f);                    // qualified, deduced
int32 e = Math::Twice<int32>(4);                // qualified, written
```

Function templates of one name form an overload set with the functions of that name (section 5). A
function template may be declared before it is defined (`template<T> T Twice(T v);` ... `template<T> T
Twice(T v) { return v + v; }`); the two are one template, and calls before the definition use it.

### 2.3 Templated extension methods

```c4
template<T> struct Pair { public T first; public T second; }
template<T> T Sum() extends Pair<T> { return first + second; }
[operator("+")] template<T> Vec2<T> Add(const Vec2<T>& o) extends Vec2<T> { ... }

Pair<int32> p;
int32 s = p.Sum();              // T deduced from the receiver (D-21)
Vec2<float> w = u + v;          // operators work the same way
```

The receiver decides the arguments its `extends` pattern names; an instance gets every such extension
when it is created, so member calls and operators find them like any extension method. A parameter the
receiver does not decide is deduced from the call (`template<T, U> U Scaled(U f) extends Pair<T>` called
as `p.Scaled(2.5f)`). A type deriving from an instance (`struct IntPair : Pair<int32>`) gets the
extension through its base. Non-template extensions on one instance (`extends Stack<int32>`) and on
primitives keep working.

### 2.4 Alias templates

```c4
template<T> alias TplVec = T[8];
template<T> alias TplPtr = T*;

TplVec<int32> a;     // int32[8]
TplPtr<int32> p;     // int32*
```

Aliases are transparent: `TplPtr<int32>` is exactly the type `int32*`.

### 2.5 Member function templates

A function template declared in a class (a plain one or a class template) is a template of that class:

```c4
struct Plain {
	public int32 k;
	template<U> public static int32 Kind(U u) { return sizeof(U); }
	template<U> public U Scale(U u) { return u * (U)k; }
	template<U> public U Offset(U u);             // defined out of line
}
template<V> V Plain::Offset(V v) { return v + (V)k; }

template<T> struct Holder {
	public T base;
	template<U> public T Plus(U u) { return base + (T)u; }
	template<U> public U Later(U u);
}
template<X> template<U> U Holder<X>::Later(U u) { return u + (U)base; }   // the class's header, then its own

Plain p;
p.Scale(2);              // U deduced
p.Scale<int64>(3);       // written
Plain::Kind(1.0f);       // static, through the type
Holder<int32> h;
h.Plus(2.5f);            // T is the instance's int32, U is float
```

- It is called on an object, through its type when static (`Plain::Kind(1)`, `Holder<int32>::F(1)`), or
  unqualified from the class's own methods; a derived class inherits it. Its instances are methods of the
  class: they see `this`, the class's members and, in a class template, its parameters.
- Written arguments (`p.Scale<int64>(3)`) name only templates, as for free functions.
- It cannot be `virtual` (it has an instance per argument list, so no vtable slot), and an explicit
  specialization of it is not written inside the class.

---

## 3. Out-of-line template methods

**Status:** **Implemented**

A method declared inside a template class may be defined later, in the same file or another
file. Two spellings are accepted:

```c4
template<T> struct TC_Vec { public T x; float Len(); };

// Owner written with its parameters (C++ style):
template<T> float TC_Vec<T>::Len() { return (float)x; }
```

```c4
template<T> class Box {
	T* data;
	int32 count;
	[operator("create")] public void Init(int32 cap);
	public int32 ByteSize();
	public void Set(int32 v);
	public void Set(float v);
}

// Owner written bare - the template's parameters are implied:
template<T> public void Box::Init(int32 cap) { this->data = (T*)raw_alloc(cap * sizeof(T)); this->count = cap; }
template<T> public int32 Box::ByteSize() { return count * sizeof(T); }
// The definition may name the parameter differently:
template<U> void Box<U>::Set(float v) { ... }
```

- A definition attaches to the declaration with the same name and **parameter types** (overloads of one
  arity are told apart), for every instance.
- A member function template of a class template is defined with both headers, the class's first:
  `template<T> template<U> U Holder<T>::Later(U u)` (section 2.5).
- The methods of each instance are their own functions with their own symbols, and so are its static data
  members: `Counter<int32>::hits` and `Counter<int64>::hits` are distinct objects. A constant static folds
  with the instance's arguments (`static const int64 Size = sizeof(T);`); one that starts at run time starts
  before main, unordered relative to other globals, as in C++.
- Parameter names, and the template parameter names, in the definition may differ from the declaration.
- Operator and constructor methods (`[operator("create")]`, `[operator("add")]`, ...) can be
  defined out of line the same way.

---

## 4. Instantiation semantics

**Status:** **Implemented**

- **Lazy, on first use.** A template is instantiated the first time a name with template
  arguments is resolved (`Box<int32>` in a declaration, `F<int32>(x)` or a deduced `F(x)` in a call, a
  template base). Instantiations are cached by definition and resolved arguments and shared
  program-wide: `Box<int32>` written in two files is one type, and so are `Buf<>` and `Buf<int32, 4>`.
- **Members are compiled when used (D-16).** An instance's member function is lowered and type-checked
  only when something uses it: a call, its address, or - for a virtual member - the instance's vtable.
  A method that only makes sense for some `T` (one that uses `<` on `T`) does not stop an instance whose
  `T` lacks it, as long as that method is not called; when it is called, its errors are reported then, each
  ending with the instance and the first place the program uses it: `(in 'Box<Owner>::Put', used at main.cf
  (14:2))`.
  Non-template code is always compiled.
- **Bodies can live in `.cf` files.** Because the whole program compiles as one unit
  ([01 - Program model](01-program-model.md)), a function template or template method may be
  declared in a header and defined in a source file, unlike C++.
- **Distinct instantiations are distinct types.** `Box<int32>` and `Box<uint64>` have
  independent layouts, statics and methods, and coexist freely.
- **No AST rewriting.** The compiler lowers the template body with a scope layer that binds
  each parameter (`T := int32`, `N := 5`); see
  [compiler/02-program-model.md](../compiler/02-program-model.md).
- **Names resolve where they are written.** The arguments of a use resolve at the use, so inside
  `namespace N` the unqualified `Lib::Box<Point>` and `Buf<int32, kSize>` mean `N::Point` and `N::kSize`. A
  template's body and its parameters' defaults resolve where the template is declared: after its
  parameters, the names of its own namespace (or class), whichever namespace the use is in.
- **`sizeof` of an instantiation is a constant.**
  `const int32 SZ = sizeof(Pair<float>);` folds at compile time.
- Using template arguments on a non-template is an error: `not a template: Plain`.

---

## 5. Argument deduction

**Status:** **Implemented** (D-17)

A function template's arguments that the call does not write are deduced from the call's argument types,
by matching each parameter's type as written against its argument:

| Parameter | Argument | Deduces |
|---|---|---|
| `T` | `int32` | `T = int32` |
| `T` | `int32[4]` | `T = int32*` (an array decays to a pointer to its element) |
| `const T&`, `T&` | `float` | `T = float` |
| `T*` | `Node*`, or an array `Node[8]` | `T = Node` |
| `T[N]&` | `int32[7]` | `T = int32`, `N = 7` |
| `Box<T>` / `const Box<T>&` | `Box<int64>`, or a type deriving from `Box<int64>` | `T = int64` |
| `funcptr<R(A)>` | a function or function pointer `int32(int32)` | `R = int32`, `A = int32` |

Rules:

- Written arguments come first (`Convert<float>(3)` writes `R`, deduces `A`); a parameter that is written
  is not deduced - the argument converts to it (`Identity<uint64>(99)`).
- A parameter type that names no template parameter, or does not fit its argument, deduces nothing;
  overload resolution then judges the conversion.
- Two arguments that decide one parameter differently are an error naming both:
  `conflicting deductions for template parameter 'T': 'int32' and 'float'`.
- A parameter nothing decides takes its default (`template<T = int32> T Zero()`, `Zero()` is
  `Zero<int32>()`).
- **Overloads.** The function templates of a name join the overload set of the functions of that name:
  each template whose arguments are all found is a candidate. Among equally good candidates a function
  written as it is beats a template instance (`Pick(5)` calls `int32 Pick(int32)`, not `template<T> int32
  Pick(T)`); a call that writes template arguments (`Pick<int32>(5)`) names only templates. Between equally
  good template candidates the more specialized template wins by partial ordering (section 7): `F(T*)`
  beats `F(T)` for a pointer, and `F(T)` beats `F(Ts...)` for one argument. Two candidates neither more
  specialized than the other are ambiguous.
- An unqualified call finds the templates of the innermost enclosing namespace that declares the name, else
  every template of that name; a qualified call (`Math::Twice(2)`) finds those of that namespace.
- **A target type.** A function template's name written where a funcptr or func is expected deduces its
  arguments from that type's signature - its parameter types and its return type - and is the address of
  that instance:

```c4
template<T> T Identity(T v) { return v; }
template<R, A> R Convert(A a) { return (R)a; }
int32 Apply(funcptr<int32(int32)> f, int32 v) { return f(v); }

funcptr<int32(int32)> f = Identity;     // Identity<int32>
funcptr<float(int32)> c = Convert;      // R = float from the return type, A = int32
f = Identity;                           // an assignment
int32 r = Apply(Identity, 5);           // a call argument: the parameter's signature
func<int32(int32)> g = Identity;        // a func takes the instance's address
```

  This applies to a variable's initializer, an assignment, a `return`, a braced entry and a call argument.
  Among overloads the one whose parameter's signature has an instance is chosen. The signature must be one
  the template can be instantiated to (`no instance of the template 'Identity' has the signature ...`);
  where nothing says which instance (`int32 x = Identity;`, a C variadic argument) the name is an error that
  asks for the template arguments.

---

## 6. Variadic templates

**Status:** **Implemented** (D-19)

A template parameter written `...Name` is a **pack**: it takes any number of arguments, including none. A
template has at most one pack and it is its last parameter.

```c4
template<...Ts> struct List { static const int64 Size = sizeof...(Ts); }
template<T, ...Rest> struct First { public T value; }   // a pack after fixed parameters
template<...int32 Ns> struct Seq { }                    // a value pack

List<int32, float, int8>   // Ts = int32, float, int8
List<>                     // Ts is empty
Seq<1, 2, 3>
```

A function parameter whose type names a pack and is written `Ts... args` is a **function parameter pack**:
it is the function's last parameter and takes the remaining arguments, deducing the type pack from them.

```c4
template<...Ts> int64 Count(Ts... args) { return sizeof...(args); }
template<T, ...Rest> int64 CountRest(T first, Rest... rest) { return sizeof...(Rest); }
Count(1, 2.0f, 'c');      // Ts = int32, float, char
Count<int32, int64>(1, 2) // written
```

### 6.1 Expansions

`pattern...` stands for the pattern once per element, each pack it names meaning that element. It may
appear in the arguments of a call, a construction or `new`, the entries of a braced list, and a template
argument list:

```c4
template<...Ts> int32 Forward(Ts... args) { return Digits(args...); }
template<...Ts> int32 Twice(Ts... args) { return Digits(Double(args)...); }   // Digits(Double(a0), Double(a1), ...)
template<...Ts> Point Make(Ts... args) { return Point(args...); }
template<...Ts> Point* New(Ts... args) { return new Point(args...); }
template<...Ts> int32 Sum(Ts... args) { int32[sizeof...(Ts)] v = { args... }; ... }
template<...Ts> int64 Boxed() { return List<Box<Ts>...>::Size; }
template<T, ...Rest> struct Tuple<T, Rest...> { public T head; public Tuple<Rest...> tail; }
```

- Packs named in one pattern expand together and must have one length:
  `the packs 'us' (1) and 'Ts' (2) in one expansion have different lengths`.
- A pack used without `...` is an error: `the pack 'args' must be expanded with '...'`.
- `sizeof...(Name)` is the element count of a pack, an `int64` constant usable wherever a constant is
  (array sizes, template arguments, `static const` members).
- A braced list is not itself expanded (`F({args}...)`); expand inside it (`F({args...})`).

### 6.2 Fold expressions

A fold applies a binary operator across a pack, inside parentheses:

| Form | Means |
|---|---|
| `(args op ...)` | `a0 op (a1 op (... op an))` |
| `(... op args)` | `((a0 op a1) op ...) op an` |
| `(args op ... op init)` | `a0 op (... op (an op init))` |
| `(init op ... op args)` | `((init op a0) op ...) op an` |

```c4
template<...Ts> int32 Sum(Ts... args) { return (args + ... + 0); }
template<...Ts> bool All(Ts... args) { return (args && ...); }
template<...Ts> void Each(Ts... args) { (Record(args), ...); }      // calls Record per element, in order
template<...int32 Ns> struct Seq { static const int32 Total = (Ns + ... + 0); }
```

- The operators are the arithmetic, bitwise and shift operators, the comparisons, `&&`, `||` and `,`. An
  operand binds tighter than the fold's operator (`(a * b + ...)` folds `a * b`).
- `&&` and `||` stop at the first element that decides the result; `,` evaluates each element in order and
  its value is the last one.
- An empty pack folds `&&` to `true`, `||` to `false` and `,` to no operation; any other operator needs an
  initial value: `the fold '(a + ...)' is over an empty pack: give it an initial value`.
- In a binary fold exactly one operand names a pack.
- A fold cannot assign (`(args = ...)`); apply the assignment per element with a comma fold:
  `((args = value), ...)`.
- A fold over constants is a constant.

---

## 7. Specialization

**Status:** **Implemented** (D-20)

An object template may have **full** (`template<>`) and **partial** specializations; a function template
may have full specializations. A specialization is declared after its primary, in the same scope, with the
primary's name and the arguments it is for:

```c4
template<T> struct Kind { static const int32 V = 0; }
template<T> struct Kind<T*> { static const int32 V = 1; }          // partial
template<> struct Kind<int32> { static const int32 V = 2; }        // full
template<T> struct Kind<const T*> { static const int32 V = 3; }    // partial, more specialized than T*
template<T, int32 N> struct Kind<T[N]> { static const int32 V = 4; }

template<...Ts> struct Tuple { }
template<T, ...Rest> struct Tuple<T, Rest...> { public T head; public Tuple<Rest...> tail; }

template<T> T Identity(T v) { return v; }
template<> int32 Identity<int32>(int32 v) { return v + 1000; }    // the body for int32
```

- An instance uses the **most specialized** specialization whose pattern its arguments match, else the
  primary. "More specialized" is C++ partial ordering: a pattern that matches every argument list another
  matches (and not the reverse) is more specialized. `const T*` is more specialized than `T*`, a written
  type than a parameter, and a pattern without a trailing pack than one with it (`Shape<T>` beats
  `Shape<T, R...>` for one argument).
- Two matching specializations neither more specialized than the other are an error naming both:
  `ambiguous specializations of 'S': ...`.
- A top-level `const` is not part of a type (D-30), so `Kind<T const*>` is the pattern `Kind<T*>`:
  `'Kind<T*>' and 'Kind<T const*>' are the same specialization of 'Kind' ...: define it once`.
- A specialization gives an argument for each parameter of its primary; a pack expansion may only end the
  list (`Tuple<T, Rest...>`), and after the primary's fixed parameters.
- A specialization is a separate definition: its members are its own (`Store<T*>` may add methods
  `Store<T>` lacks). Its methods may be defined out of line with its pattern: `template<T> int32 Store<T*>::Tag() { ... }`.
- A full specialization of a function template replaces the body for its arguments, and must have the
  primary's signature for them: `the specialization 'Identity<int32>' is 'int64 (int32 v)', but 'Identity'
  for those arguments is 'int32 (int32 v)'`. Function templates are not partially specialized
  (`the function template 'Pick' cannot be partially specialized: overload it instead`); overloads are
  ranked by the same partial ordering (section 5).

---

## 8. Other gaps

| Item | Status | Current behavior |
|---|---|---|
| Lambdas as template arguments, template template parameters | **Open question** | Not designed. |

---

## 9. Guidance for library authors

- A template method may use any operation on `T`; it only has to compile for the instances that call it.
- Put the template declaration in the `.ch` and method bodies wherever is clearest; both
  placements compile identically.
- Use aliases to give instantiations stable public names (`alias FVec2 = TVector2<float>;`), the
  pattern a game engine uses for its vector types.
