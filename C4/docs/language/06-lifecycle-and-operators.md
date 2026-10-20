# 6. Object lifecycle and operators

The largest syntax change from C++ (AGENTS.md item 3): **constructors, destructors and operators are one
concept**. Each is an ordinary named method (or extension method) tagged with an `[operator(...)]`
annotation. There is no `Type()` constructor syntax, no `~Type()`, and no `operator+` function name.

```c4
class Buffer {
	[operator("create")] public void Init();               // default constructor
	[operator("create")] public void Init(uint32 cap);     // constructor overload
	[operator("delete")] public void Destroy();            // destructor
	[operator("+", "add")] public Buffer Add(const Buffer& o);   // operator: token or name
	[operator("hash")] public uint32 Hash();               // custom operator
};
```

---

## 6.1 The `[operator(...)]` annotation

**Status:** **Implemented**

- The argument is either the operator **token** (`"+"`, `"[]"`, `"+="`) or its **canonical name**
  (`"add"`, `"index"`, `"add_assign"`). Both spellings are the same operator.
- One function may carry several operators when their signatures fit:
  `[operator("+", "add")]`, AGENTS.md's `[operator("=", "copy")]`. Every name is recorded: the method of
  `[operator("=", "copy")]` runs for `a = b`, for a copy, and for `a.operator_assign(b)` and `a.operator_copy(b)`.
- An operator must be a **method** of the type or an **extension method** (`extends T`). A free
  function with `[operator(...)]` and no `extends` is not an operator.
- Operators are inherited: an operator declared on a base (including a template base) applies to the
  derived type.
- Because operators are named methods, generic code can call any operator without knowing the method
  name: `v.operator_add(v2)`, `p->operator_hash()` (6.9).

## 6.2 Construction: `create`

**Status:** **Implemented**

```c4
class Counter {
	public int32 value;
	[operator("create")] public void Init() { value = 1; }
	[operator("create")] public void Init(int32 initial) { value = initial; }
}

Counter a;                      // zeroed, then the default create (no arguments, or all defaulted)
Counter b(5);                   // overload resolution over Counter's own creates
Counter c = Counter(7);         // T(args) builds a temporary, which moves into c (no copy)
Counter d = Counter(c);         // T(x) with x a T: a copy of c (6.4)
Counter* h = new Counter(3);    // heap: allocated zeroed, then the create runs with this = h
Counter* p = new (raw) Counter(4); // placement: built in existing storage (14.1)
Core::Array<int32> v = {1, 2, 3}; // braced: the create taking __init_list<T>
```

Construction of a `T` runs, in order (D-07, the C++ order):

1. The storage is zeroed and gets its vptrs (see [04-types](04-types.md#410-initialization)).
2. The class base is constructed: by the base create that the create body calls as its **first
   statement** (`Base::Init(x);`), otherwise by the base's 0-argument create. A base that declares
   creates but no 0-argument one must be called that way by every create `T` declares:
   `'Base' has no create that takes no arguments; call one as the first statement of the create of 'T'`.
   A `T` that declares no create builds such a base the way `Base b;` does (steps 1-3 of the base).
3. Each member starts in declaration order: from its initializer when it has one
   ([05-objects](05-objects.md#52-members)), otherwise by its default construction (its 0-argument
   create; an array element by element). Members of an anonymous union are not constructed.
4. The create's own body runs.

Which create runs:

- **No arguments** (`T v;`, `T()`, `new T`): `T`'s 0-argument create. A type without one still gets steps
  1-3, so `T v;` is valid for every type.
- The create that runs must be accessible where the construction happens ([05 5.7](05-objects.md#57-visibility)):
  a private create builds objects only inside the type, as a factory does.
- **Arguments**: overload resolution over `T`'s **own** creates (creates are not inherited), with default
  arguments filled. No match: `no matching constructor of 'T' with N argument(s)`.
- **One argument that is itself a `T`**, when no create takes a `T`: a copy of it, or a move of a
  temporary (6.4).
- **A braced list** goes to a create taking `__init_list<E>` when `T` has one.
- **A type with no create at all** is an aggregate: the arguments (or braced entries) initialize the
  fields in order, fields they do not reach start from their initializers or are default-constructed, and
  more arguments than fields is `too many initializers for 'T'`. A braced list can also name fields
  (`{ y = 4 }`, [10-expressions](10-expressions.md#9-braced-initialization)); a type with creates takes
  its arguments in order only.

A type that declares no create but has a base or member with one gets a compiler-written create
(`__c4_create_T_<n>`) that runs steps 1-3.

**Exception safety:** when a create (or a copy, 6.4) throws, the members and base it already built are
destroyed in reverse order and the exception continues; `T`'s own `delete` does not run (the C++ rule).
An initializer list that throws destroys the fields or elements it already filled. Parts not built yet
are still zero, and destroying a zero part does nothing. A `noexcept` create is not guarded.

## 6.3 Destruction: `delete`

**Status:** **Implemented**

`[operator("delete")]` is the **only** destructor (D-03). `[operator("destroy")]` and
`[operator("finalizer")]` are errors that name it. It takes no parameters.

Destruction of a `T` runs, in order: its `delete` body; each member's destruction in reverse declaration
order; the class base's destruction. A type with parts to destroy and no `delete` of its own gets a
compiler-written one (`__c4_delete_T_<n>`).

The same sequence runs for every way an object ends:

- **a value local** - when its scope ends, by every edge (falling off the end, `return`, `break`,
  `continue`, `goto`, an exception), in reverse declaration order (scheduled like a `defer`, see
  [09-statements-and-control-flow](09-statements-and-control-flow.md));
- **a by-value parameter** - when the callee returns: the callee owns its parameters;
- **a temporary** - at the end of its full expression (6.4);
- **a heap object** - on `delete p;`, or when the collector reclaims it, exactly once (registered by `new`,
  see [../runtime/01-memory-and-gc.md](../runtime/01-memory-and-gc.md));
- **an object in raw storage** - on `destroy(p)` ([14.1](14-builtins.md#1-compiler-intrinsics));
- **a thrown object** - after the last handler is done with it.

**Zero is a valid value of every type** (D-05): a `delete` must accept a zeroed object. A moved-from value
is zero, and destroying it must do nothing.

## 6.4 Copy, move and assignment

**Status:** **Implemented**

**Copy.** `[operator("copy")] public void CopyFrom(const T& other);` builds a new object from `other` in
zeroed storage. The base is copied first, by its own copy, unless the body's first statement calls one
(`Base::CopyFrom(other);`); the members start zero and the body sets them. A copy operator takes exactly
one `const T&`.

- A type without a copy operator is copied member by member (a compiler-written `__c4_copy_T_<n>`); a type
  with nothing to copy or destroy copies its bytes.
- A type that has a `delete` (itself or in a part) but no copy operator is **not copyable**: copying it is
  `'T' cannot be copied: it has [operator("delete")] but no [operator("copy")]; add a copy operator, or move
  the value with take(x)`.
- A copy is made when an object is initialized from a variable, field, element or reference; passed by
  value from one; returned when it is a field, global or reference; thrown from a variable; or caught by
  value.

**Move.** C4 has no `T&&`, no move constructor and no value categories (D-05). Objects are **trivially
relocatable** - a type must not store its own address - so moving an object is copying its bytes and
forgetting the source:

- A **temporary** - a by-value call result, `T(...)`, a copy, `take(x)`, an object `?:` - moves into what it
  initializes or is passed to. It is never copied.
- `return local;` (or a by-value parameter) moves the value to the caller; the local is not destroyed.
- `take(x)` moves the value out of any variable, field or element and leaves `x` zero. `[operator("move")]`
  is an error that points here.
- A **local or by-value parameter at its last use** moves instead of being copied, when its address never
  escaped: `Res b = a;`, `Sink(a);` or `c = a;` with no later use of `a` take `a`'s object, and `a` is not
  destroyed. A use is any read or write; a use in the other arm of an `if` or `?:` does not follow it; a copy
  inside a loop of a variable declared outside the loop is not a last use; a variable a `defer` uses, and every
  variable of a function with a `goto`, keep their copies. The address escapes through `&a`, a reference
  variable (`T& r = a;`) or a reference capture - passing `a` to a reference parameter or calling a method on
  it does not. A moved-from variable is destroyed on no path: the move clears its drop flag, which its cleanup
  tests (an exception after the move destroys the object once, through its new owner).

**Assignment.** `a = b` on objects (D-06):

- runs the type's `[operator("=")]` / `"assign"` when it declares one;
- otherwise, for a type with parts to copy or destroy, makes the new value first (a copy of `b`, or `b`
  itself when it is a temporary), then destroys `a`'s old value and moves the new one in - so `a = a` and
  `a = f(a)` are safe;
- copies bytes for any other type.

### Temporaries

An object that a full expression creates and does not keep - a call result used for a member access or
bound to a `const T&` parameter, or a discarded result - lives until the end of that full expression, then
it is destroyed (last created, first destroyed). The full expression is:

- the statement, for an expression statement, a declaration (the declared variable lives on) or a
  `return`;
- the condition itself, for `if`, `switch`, `while`, `do` and `for`: its temporaries are destroyed before
  the body or branch runs, and again after each later evaluation;
- each evaluation of a `for` loop's step, and the `for` loop's init.

An exception leaving a full expression destroys its temporaries too.

## 6.5 The operator table

**Status:** **Implemented** (the catalog lives in `C4Lib/src/Model/OperatorTable.cpp`)

| Token | Binary name | Unary name |
|---|---|---|
| `+` | `add` | `unary_plus` |
| `-` | `subtract` | `negate` |
| `*` | `multiply` | `dereference` |
| `/` | `divide` | - |
| `%` | `modulo` | - |
| `**` | `power` | - |
| `==` | `equal` | - |
| `!=` | `notequal` | - |
| `<` | `less` | - |
| `>` | `greater` | - |
| `<=` | `less_equal` | - |
| `>=` | `greater_equal` | - |
| `&&` | `logical_and` | - |
| `\|\|` | `logical_or` | - |
| `!` | - | `logical_not` |
| `&` | `bitwise_and` | `address_of` |
| `\|` | `bitwise_or` | - |
| `^` | `bitwise_xor` | - |
| `~` | - | `bitwise_not` |
| `<<` | `shift_left` | - |
| `>>` | `shift_right` | - |
| `[]` | `index` | - |
| `[]=` | `index_assign` | - |
| `()` | `call` | - |
| `+=` `-=` `*=` `/=` `%=` | `add_assign` `subtract_assign` `multiply_assign` `divide_assign` `modulo_assign` | - |
| `&=` `\|=` `^=` | `and_assign` `or_assign` `xor_assign` | - |
| `<<=` `>>=` | `shift_left_assign` `shift_right_assign` | - |
| `++` | - | `increment` |
| `--` | - | `decrement` |

Lifecycle and protocol names without a token: `create`, `delete`, `copy`, `cast`, `begin`, `end`; the
assignment operator is `=` or `assign` (6.4).

`call` takes any number of arguments: `x(a, b)` on an object calls its `[operator("call")]` method
([07](07-functions-and-lambdas.md#712-the-call-operator)).

`**` is built in for numbers (docs/language/10); `[operator("power")]` gives an object one.

## 6.6 Arity and resolution rules

**Status:** **Implemented**

- **Arity picks the meaning (D-24).** `-` with one operand is `negate`, with two is `subtract`; `*` is
  `dereference` or `multiply`; `&` is `address_of` or `bitwise_and`; `+` is `unary_plus` or `add`. In an
  annotation the method's parameter count decides: `[operator("-")]` on a method with no parameter is
  `negate`, with one it is `subtract`. The names (`"negate"`, `"subtract"`) are strict.
- **Every name counts.** `[operator("less", "greater")]` or `[operator("&", "bitwise_and")]` registers the
  method under each of its names.
- **Receiver (D-25):** for `a op b` the operator is a method of `a`'s type: its own methods, inherited
  methods and extensions, among those that can take `b`. The operands are never swapped, so `v - 2.0f`
  and `2.0f - v` are different operators. A scalar on the left with an object on the right is written as
  an extension method on the scalar type:

  ```c4
  [operator("*")] Vec2 Times(const Vec2& v) extends float { return Vec2(*this * v.x, *this * v.y); }
  Vec2 w = 2.0f * v;   // Times on the float
  ```

  With no such method the error says so: `no operator '*' takes a 'float' on the left and a 'Vec2' on the
  right: an operator is a method of its left operand's type; ...`.
- **Compound fallback:** `a += b` uses `add_assign` when declared, otherwise `a = a + b` through `add`.
  `and_assign` / `or_assign` / `xor_assign` fall back to the bitwise operators.
- **Increment / decrement:** one annotation serves prefix and postfix. Postfix yields the old value.
- Two primitive operands always use the built-in operator; an extension on a primitive type is used only
  when the right operand is an object.
- An overloaded `&&` / `||` is a method call, so expect both operands to be evaluated (no
  short-circuit), as in C++.

## 6.7 Indexing: `index` and `index_assign`

**Status:** **Implemented**

```c4
[operator("index")]
float GetIndex(int32 i) extends Point { if (i == 0) return this.x; return this.y; }

[operator("index_assign")]
void SetIndex(int32 i, float v) extends Point { if (i == 0) this.x = v; else this.y = v; }

// Or one reference-returning indexer (Bedrock Core::Array<T>):
[operator("index")] public T& At(int64 i) { return data[i]; }
```

- `x[i]` reads through `index`.
- `x[i] = v` uses `index_assign` when declared, or writes through the reference when `index` returns
  `T&`.
- Writing through a by-value `index` with no setter is an error that names the fix
  (`Add a setter, or make the indexer return a reference.`).

## 6.8 Conversion operator: `cast`

**Status:** **Implemented**

```c4
[operator("cast")]
float ToFloat() extends Point { return this.x + this.y; }

Point p;
float f = (float)p;      // calls ToFloat
```

A `cast` method takes no parameters; its return type is the conversion target. It overrides the
C-style cast `(T)x` for that target, on objects and (through extensions) on primitives. Conversions are
explicit only (D-32): `float f = p;` with a cast operator on `p`'s type is an error that names `(float)x`.

## 6.9 Custom operators and generic operator calls

**Status:** **Implemented**

Any name that is not in the table is a **custom operator**:

```c4
struct Item {
	int32 id;
	[operator("hash")] public uint32 Hash() { return (uint32)id; }
};

Item it;
uint32 h = it.operator_hash();     // calls Hash, whatever it is named
Item* p = &it;
uint32 h2 = p->operator_hash();
```

`x.operator_<name>(args)` calls the method annotated `[operator("<name>")]` on `x`'s type (members,
inherited methods and extensions), and works for built-in operator names too (`a.operator_add(b)`).
This is how libraries interact with user types without imposing naming conventions.

Bedrock's `Core::HashMap<K, V>` hashes a key through `key.operator_hash()` and compares it with `==`
(its default `Core::KeyTraits<K>`, with built-in traits for numbers and pointers; see
[../library/bedrock.md](../library/bedrock.md)).

## 6.10 Iteration protocol: `begin` / `end`

**Status:** **Implemented**

Range-for over a user type uses operators instead of C++'s `begin()` / `end()` naming convention:

```c4
template<T>
struct VectorIterator {
	public T* items;
	public uint32 index;
	[operator("notequal")] public bool Noteq(const VectorIterator<T>& o) const { return this.index != o.index; }
	[operator("dereference")] public T& Deref() { return this.items[this.index]; }
	[operator("increment")] public VectorIterator<T> Inc() {
		VectorIterator<T> r; r.items = this.items; r.index = this.index + 1; return r;
	}
};

// On the container:
[operator("begin")] public VectorIterator<T> Begin();
[operator("end")]   public VectorIterator<T> End();

for (int32 v : vec) { ... }
```

`for (T x : c) body` lowers to (pseudo-code):

```text
it = c.begin(); end = c.end();
while (it notequal end) { T x = dereference(it); body; it = increment(it); }
```

- The container needs `begin` and `end`; the iterator needs `notequal`, `dereference` and `increment`.
- `increment` returns the next iterator (the loop assigns it back).
- `continue` advances the iterator.
- A missing piece is a clear error (`iterator protocol: 'begin' has no body`).
See [09-statements-and-control-flow](09-statements-and-control-flow.md) for range-for over arrays.

## 6.11 Differences from C++

| C++ | C4 |
|---|---|
| `T()` / `T(args)` constructors | `[operator("create")]` methods (any name) |
| `~T()` | `[operator("delete")]` method |
| copy constructor `T(const T&)` | `[operator("copy")]` taking `const T&`; member by member when absent |
| `T& operator=(const T&)` | `[operator("=")]` or `"assign"`; when absent: copy, destroy the old value, move the new one in |
| move constructor, `std::move`, `T&&` | none: objects relocate; temporaries and returned locals move, `take(x)` moves explicitly |
| member initializer lists | members are default-constructed; the base create (with arguments) is the create's first statement |
| placement new, `std::destroy_at`, relocation | `new (p) T(...)`, `destroy(p)`, `relocate(dst, src)` ([14.1](14-builtins.md#1-compiler-intrinsics)) |
| `operator+` function names | `[operator("+")]` or `[operator("add")]` on any method |
| `begin()` / `end()` naming convention | `[operator("begin")]` / `[operator("end")]` |
| `operator T()` | `[operator("cast")]`, explicit casts only |
| `std::hash` specialization | `[operator("hash")]` custom operator |
| free-function operators | must be members or `extends` extension methods |

## 6.12 Static storage

**Status:** **Implemented**

Globals, static members and static locals live for the whole program.

```c4
class Log { [operator("create")] void Open() { ... } [operator("delete")] void Close() { ... } }

Log g_log;                         // started before main
int32 g_limit = ComputeLimit();    // started before main, after g_log
int32 g_count = 4;                 // a constant: no code runs

Log& Shared() {
	static Log log;                // started the first time control reaches it
	static int32 calls = 0;        // a constant: no guard
	calls++;
	return log;
}
```

- **Start.** A global, static member or static local whose initializer is a constant starts holding it,
  converted to the variable's type as any initialization converts it (`const float half = 1;` holds `1.0`,
  and the same conversion errors and warnings apply); `null` and a string literal are constant starts for a
  pointer. Any other global or static member starts before `main`, in declaration order (the files in build
  order, each top to bottom): from its initializer, by its braced list, or by its default construction
  (6.2). Any other static local starts the first time control reaches its declaration, and never again; one
  that starts as a constant costs nothing per pass.
- **Threads.** A static local's start runs once even when threads reach it together: the others wait for
  it. Every later pass costs one load. A `thread_local` variable starts once on each thread instead (04 4.13).
- **A start that throws** leaves the static unstarted and zero again (the parts it built are destroyed);
  the exception continues, and the next pass tries again. A global's start that throws stops the program
  (`an exception was thrown and not caught`). A static local whose start reaches its own declaration
  again (recursion) stops the program: `a static local's initialization reached its own declaration
  again`.
- **End.** Each object that needs destruction is registered when its start completes, and they are
  destroyed after `main` returns (or on `exit`), the last started first; an array's elements last first.
  An exception escaping one of these destroys stops the program: `an exception escaped the destruction
  of a global or static object at exit`.
