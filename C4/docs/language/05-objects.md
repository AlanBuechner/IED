# 5. Objects: classes, structs, interfaces, unions

C4 objects work like C++ classes: they are value types, they have fields and methods, they can inherit
and have virtual functions. The differences are in visibility syntax, the inheritance model (one base
class plus interfaces), and the fact that constructors, destructors and operators are ordinary methods
tagged with `[operator(...)]` (covered in [06-lifecycle-and-operators](06-lifecycle-and-operators.md)).

---

## 5.1 Object kinds

**Status:** **Implemented**

| Kind | Default member access | Purpose |
|---|---|---|
| `class` | `private` | general types with encapsulated state |
| `struct` | `public` | plain data, small value types |
| `interface` | `public` | abstract contracts made of virtual methods (5.8) |
| `union` | `public` | overlapping storage (5.6) |

```c4
struct TestPoint {
	int32 x;
	int32 y;
};

class Counter {
	int32 value;
	public void Inc() { value++; }
	public int32 Get() { return value; }
};
```

- The trailing `;` after a type body is optional (`struct Box { int32 v; }` is fine).
- Objects are **value types**: `TestPoint a = b;` copies, parameters of object type are passed by value
  unless declared `T&` / `const T&` / `T*`.
- `class Window;` (a named type with no body and no base list) is a forward declaration introducing an
  incomplete type. Pointers and references to it work; `sizeof` of it is `sizeof of incomplete type`.
  Forward declarations are only needed inside function bodies (see
  [01-program-model](01-program-model.md#13-program-model-rules)) or to state that a type is opaque.
- Anonymous `struct { ... };` is allowed inside a union to group a view's fields.

## 5.2 Members

**Status:** **Implemented**

A type body may contain fields, methods, `packed` / `unpacked` lane views
([11-vectors-and-swizzling](11-vectors-and-swizzling.md)), nested `class` / `struct` / `interface`,
`enum`, `union` and `alias` declarations.

- Methods can be defined inline or declared in the body and defined out of line
  (`float Shape::GetArea() { ... }`), see [07-functions-and-lambdas](07-functions-and-lambdas.md#72-declarations-definitions-and-out-of-line-methods).
- Inside a method, members are reachable bare (`x`) or through `this`; `this->x` and `this.x` both
  work. `this` names the receiver: `*this` is the object, and wherever a pointer is wanted - a return, an
  initializer, an argument, a comparison, a cast, `as` and `is`, a branch of `?:` beside a pointer, `delete this`,
  `sizeof(this)` - it is the receiver's address, a `T*` (a `const T*` in a const method), as in C++.
- All fields are visible to all method bodies regardless of declaration order, so a C++-style trailing
  `private:` data section works.
- Several fields can share one declaration: `public float x, y, z;`.
- A field may have an initializer (`public int32 count = 3;`): every construction of the object starts the
  field from it - a local, `new T`, `T(...)`, a global, an element of an array - before the create's body
  runs (6.2). It is evaluated once per object and may use earlier fields and `this`:

  ```c4
  struct Limits {
  	public int32 base = 10;
  	public int32 doubled = base * 2;  // an earlier field
  	public float scale = 1.5f;
  }
  Limits a;                   // base 10, doubled 20, scale 1.5
  Limits b = { 4 };           // base 4; doubled and scale from their initializers
  ```

  A braced list (`T v = {...}`, `T{...}`) builds no object before its entries, so the initializer of a
  field the list leaves out is evaluated without one: constants, globals and static members are visible,
  other fields and `this` are not (`... which a braced list evaluates without an object`). In `b` above,
  `doubled` would be that error; write the fields the initializer needs, or give `Limits` a create.

## 5.3 Layout

**Status:** **Implemented**

The layout is the MSVC x64 layout, so a C4 type and the equivalent C++ type have the same bytes and the
same vtable pointers (required for interop, see [13-interop](13-interop.md)):

1. Polymorphic bases first, in base-list order. The first one is the **primary base**: it sits at offset
   0 and its vtable pointer is the type's own.
2. If the type is polymorphic but has no polymorphic base, its own vtable pointer is at offset 0.
3. Then the bases without virtual methods, in base-list order.
4. Then the fields in declaration order, with natural alignment and padding.

- Each base keeps its full padded size: a derived type's fields never reuse a base's tail padding
  (`struct B { virtual ...; int32 a; }` is 16 bytes, so `struct D : B { int32 b; }` puts `b` at 16).
- Every polymorphic sub-object (the primary chain and each other interface or polymorphic base) has its
  own vtable pointer.
- `static` fields are not part of the instance.
- A type cannot hold itself by value, directly or through other types' fields, bases or array elements: it
  would have no size (`'Node' holds itself by value ('Node.next' holds a 'Node'), so it has no size: hold one
  of them by pointer`).
- No object is larger than 2147483647 bytes (MSVC's limit too): a larger type or array is an error that
  gives its size.
- `sizeof(T)` is the padded size; reflection exposes size, alignment and field offsets
  ([../runtime/02-reflection.md](../runtime/02-reflection.md)).

## 5.4 Static members

**Status:** **Implemented**

```c4
struct Tally {
	public static int32 total;
	public static int32 MaxCount = 8;
	public void Inc() { total += 1; }
	public static Tally Make() { Tally t; return t; }
};

Tally.total = 100;          // static field: Type.field or Type::field
Tally t = Tally::Make();    // static method: Type::Method()
```

- A static field is one program-wide global; it may have an initializer (scalar or composite). It starts
  and ends like a global ([06-lifecycle-and-operators](06-lifecycle-and-operators.md#612-static-storage)).
  Its initializer finds names in the class: `public static int32 s_first = Seed();` calls the static
  method `Seed` of the same class.
- A `const` static with a constant initializer folds to a compile-time constant; any other `const` static
  (`public static const Mat2 Identity = { Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f) };`) is set at startup.
- Static methods have no object: `this`, an unqualified instance field and an unqualified instance method
  call are errors in them (`'x' is a field: the static method 'Get' has no object`).

## 5.5 Nested and local types

**Status:** **Implemented**

- Types nested in a type are named `Outer::Inner` from outside.
- Types (and aliases and functions) may be declared inside a function body; they are scoped to the
  enclosing block and must appear before their use in the body.

## 5.6 Unions

**Status:** **Implemented**

```c4
union Value {
	int32 intValue;
	float floatValue;
};

struct Color {
	public union {
		struct { float r; float g; float b; };
		struct { float x; float y; float z; };
	};
};
```

- A named `union` is a type whose fields share one storage region. It holds fields (each at offset 0) and
  struct views (`struct { float x; float y; };`, whose fields follow each other from offset 0); its size is
  the largest member's, padded to the strictest alignment. Member types resolve where the union is declared,
  so a union in `namespace N` names `N`'s types.
- A named union cannot hold a lane view (`packed float x, y;` goes in an anonymous union inside a class), a
  union, or a static member; each is an error at the member.
- An **anonymous** union inside an object flattens its fields into the object at one offset, so
  `c.r` and `c.x` are the same storage. Struct views inside a union group fields.
- Arrays as union members use C4 declarator syntax: `char[4] bytes;` (not `char bytes[4]`).
- `packed` / `unpacked` lane views inside a union are how SIMD vector types are written - see
  [11-vectors-and-swizzling](11-vectors-and-swizzling.md).

## 5.7 Visibility

**Status:** **Implemented**

C4 supports both C++ section labels and C#-style per-member modifiers (AGENTS.md item 7):

NOTE: im still a bit iffy on this as it is re defining the same thing but the c++ style tags help with grouping and forcing you to think about the organization where as the inline modifiers makes it easy to know what a given functions visibility is when there is a lot of stuff that pushes the c++ tag off screen and you have to hunt it down.

```c4
class Player {
public:
	public void TakeDamage(int32 amount);
	public int32 GetHealth();
private:
	private float speed;
	private int32 health;
};
```

An inline modifier always states a member's access (D-28). A `public:` / `private:` / `protected:` label
does not grant access by itself; it **requires** every member declared under it to carry the matching
inline modifier. This lets a reader see a member's visibility without hunting for the label (a problem in
very large types), keeps the visual grouping of labels, and forces whoever extends the class to think about
where a new member belongs. Outside any label, a member without a modifier gets the kind's default (5.1).

- A member under a label with no modifier is an error with the fix:
  `member 'width' is under 'public:' but has no access modifier; write 'public'`.
- A member whose modifier conflicts with the label is an error:
  `Member declares 'public' but is inside a 'private:' section`.
- Nested type declarations are exempt from section checking.
- Interface members are public: `private` / `protected` on one, or a `private:` / `protected:` label in
  an interface, is an error.

Access is checked wherever a member is used: field and static-member reads and writes, method and static
method calls, operators (including `[]`, `[]=`, casts, the range-for protocol and a callable's call
operator), and the create a construction runs: `T v(...)`, `T(...)`, `new T(...)`, and every default
construction - `T v;`, array elements, a field a create builds, a field a braced list leaves out, and a
base. A field's default construction is checked from its owner (the error points at the field).

| Access | Usable from |
|---|---|
| `public` | anywhere |
| `protected` | the type's own methods, the methods of types nested in it, and the methods of types derived from it |
| `private` | the type's own methods and the methods of types nested in it |

- "Methods" includes static methods, lambdas written inside a method, the type's field initializers and
  the default arguments of its methods (a default is checked where it is declared).
- Free functions and extension methods (5.10) are outside code: they see public members only.
- The error names the member, its kind and its type: `Cannot access private method 'Code' of type 'Vault'`,
  `Cannot access protected field 'level' of type 'Gear'`, `Cannot access private create 'Init' of type 'Token'`.
- Copies and destruction are not access-checked: a type controls them with its copy and delete operators
  (06 6.3, 6.4), not with their visibility. Calling one by name (`v.Destroy()`) is checked like any method.
- C4 has no `friend`; Bedrock makes the few fields a partner type needs public.

## 5.8 Inheritance and interfaces

**Status:** **Implemented**

C4 uses a **single inheritance, multiple interface** model (AGENTS.md item 6): a type has at most one
base class and may implement any number of interfaces.

```c4
interface IArea { int32 Area(); }
interface INamed { int32 NameLength(); }

class Shape { public int32 id; public virtual int32 Sides() { return 0; } }

class Square : Shape, IArea, INamed {
	public int32 side;
	public override int32 Sides() { return 4; }
	public int32 Area() { return side * side; }
	public int32 NameLength() { return 6; }
}

Square* sq = new Square();
IArea* a = sq;          // upcast: the IArea sub-object of the Square
INamed* n = sq;         // a different sub-object, a different address
int32 x = a->Area();    // dispatches to Square::Area
```

- Base list: `class D : B, I1, I2`, optionally with an access keyword per base (`struct C : public S`).
  The class base, if any, is written **first**; the rest are interfaces.
- An `interface` derives only from interfaces, has no instance fields (static fields are allowed), and
  its methods are public and virtual. A method may be pure (no body or `= 0`) or have a default body that
  implementers keep unless they override it.
- A base may not be listed twice, and a listed base may not already be a base of another listed base
  (`class C : B, I` where `B` already implements `I`): every base type is exactly one sub-object, so no
  conversion is ambiguous. Override the methods instead of listing the interface again.
- `Derived*` converts implicitly to a pointer to any base or interface; the address moves to that
  sub-object and `null` stays `null`. A `Derived` object binds to a `Base&` / `const Base&` the same way.
  The reverse direction needs a checked cast (`p as Derived*`, [10-expressions](10-expressions.md#7-type-tests-is-and-as)).
- A member found in two unrelated bases with the same signature is ambiguous at the use site
  (`'Size' is inherited from both 'IA' and 'IB'; declare it in 'C' to choose`).
- A base may be a template instance (`struct Circle : public Shape<float>`).
- A base is a class, struct or interface, and never the type itself or one that already derives from it
  (`'B' cannot inherit from 'A': 'A' already inherits from 'B'`).
- Interfaces are laid out like C++ abstract classes, so a C4 class can implement an interface that C++
  code calls, and the other way round ([13-interop](13-interop.md)).

## 5.9 Virtual methods

**Status:** **Implemented**

```c4
struct Shape {
	float x;
	virtual float GetArea();
};

struct Circle : public Shape {
	float radius;
	float GetArea() override;
};
```

- `virtual` introduces a virtual method; `override` marks a re-implementation (and implies virtual).
  `override` may be written as a leading modifier (`public override int32 Area()`) or after the
  parameter list (`float GetArea() override;`). A method with the same name and parameter types as an
  inherited virtual overrides it even without the keyword.
- A virtual slot is identified by **name plus parameter types**: overloads are separate slots, and an
  override replaces exactly the slot with its signature.
- A derived type keeps its primary base's slots in order (an override keeps the index) and appends the
  virtuals it introduces, in declaration order. A method that implements an interface lives in that
  interface's vtable.
- `= 0` (or no body in an interface) makes a method pure.
- Every vtable pointer of an object is set wherever the object lives: stack locals, `static` locals,
  globals, fields of other objects, array elements, heap objects and temporaries. It is set before the
  constructor runs, so a virtual call during construction reaches the type being built.
- Calls through a pointer or reference dispatch dynamically. `Base::F()` inside a method calls that one
  implementation directly, without dispatch, and so do `obj.Base::F()` and `p->Base::F()` on any receiver
  (`p->IShape::Sides()` reaches an interface's own body). The qualifier must be the object's type or one of
  its bases; `obj.Base::field` reads a field a derived class hides.
- A copy of a base sub-object is an object of the base type (it dispatches to the base's methods), and an
  assignment through a base pointer or reference copies the fields but never changes the object's
  dynamic type.

### Abstract and final

**Status:** **Implemented** (D-29)

```c4
abstract class Shape {                       // only ever a base
	public abstract float Area() const;      // virtual, with no implementation here
}
final class Square : public Shape {          // nothing derives from it
	public float side;
	public float Area() const override { return side * side; }
}
class Tagged : public INamed {
	public virtual int32 Id() const final { return 7; }   // nothing overrides it
}
```

- A type is **abstract** when it is declared `abstract`, when it is an interface, or when a virtual method
  of it has no implementation (`= 0`, an `abstract` method, a bodiless interface method no derived type
  implements). No object of an abstract type can exist: a value local, global, field, array element,
  by-value parameter or return value of one is an error (`'s': 'Shape' is abstract; use a pointer or a
  reference`), and so is creating one (`new Shape()`, `Shape()`: `cannot create a 'Shape': 'Shape' is
  abstract`). The unimplemented case names the method: `'Square' is abstract: 'Area' is not implemented`.
  Pointers and references to an abstract type are the way to use it.
- An `abstract` method is a virtual method with no body in its type; a body is an error, as is a static
  abstract method.
- A `final` class cannot be derived from (`'More' cannot derive from 'Leaf': it is final`). A `final`
  method cannot be overridden (`'F' overrides a final method of 'Base'`); only a virtual method (or an
  override) can be final. `final` is written before the return type or after the parameter list.
- `abstract final` is an error (nothing could create the type), as are `abstract interface` and `final
  interface`. `virtual`, `override`, `abstract` and `final` on a free function or an extension method are
  errors.

### Const methods

**Status:** **Implemented** (D-31)

```c4
class Counter {
	public int32 value;
	public int32 Get() const { return value; }          // `this` is read-only here
	public int32& Slot() { return value; }
	public const int32& Slot() const { return value; }  // the const twin
}
int32 Read(const Counter& c) { return c.Get() + c.Slot(); } // a const receiver calls const methods
```

- In a const method `this` is read-only: writing a field is an error (`'this' is const in the const method
  'Get'`), and so are calling a method that is not const (unqualified or through `this`) and returning a
  non-const reference to the object (`cannot return a non-const reference to a const object`).
- A const object, a `const T&` and a `const T*` call only const methods and operators: `'Set' is not a
  const method, so it cannot be called on a const object`.
- A method and a const method with the same parameters are two functions (C++ const overloading): a
  read-only receiver calls the const one, a writable receiver the other. An out-of-line definition repeats
  the `const`; an override matches its base method's constness.
- Creates, copies and deletes run on objects that are being built or ended, so they may write the object's
  const fields; destroying a const object calls its (non-const) delete.
- A lambda's call operator is const unless the lambda is `mutable`, so a closure can be called through a
  `const F&`.
- A free function cannot be const (`only a method can be const`).

## 5.10 Extension methods: `extends`

**Status:** **Implemented**

An extension method adds a method to an existing type without editing or inheriting from it. `this` is
implicit; there is no explicit self parameter.

```c4
struct Point { float x; float y; };

float Length() extends Point;                     // declaration (in a header)
void SetXY(float nx, float ny) extends Point;

void SetXY(float nx, float ny) extends Point {    // definition
	this.x = nx;
	this.y = ny;
}

[operator("add")]
Point Add(const Point& other) extends Point;      // operators may be extensions

[operator("cast")]
OpIVec2 ToIVec2() extends float {                 // primitives can be extended too
	float val = *this;                            // `this` points at the receiver
	return OpIVec2((int32)val, (int32)(val * 2.0f));
}

Point p;
p.SetXY(1.0f, 2.0f);                              // called like any member
```

- Extensions work on objects and on primitive types.
- They can be operators (`[operator("add")]`) and constructors (`[operator("create")] ... extends T`).
- The target may be namespaced (`extends NS::Box`, covered end to end by
  `assets/TestCode/Types/NamespacedExtensionMethod.cf`), a concrete template instance
  (`extends Pair<int32>`), or every instance of a template: `template<T> T Sum() extends Pair<T>` deduces
  `T` from the receiver ([08 2.3](08-templates.md#23-templated-extension-methods),
  `assets/IsolatedTest/Templates/ExtensionTemplates`).
- An extension method is called on an object, so it is never `static` (`'Make' extends a type and is static:
  an extension method is called on an object`); a static helper is a free function or a static member.
- Lookup order: the type's own methods, then inherited methods, then extensions.
- An extension is outside code: it sees only the public members of the type it extends (5.7).


## 5.11 Differences from C++

| C++ | C4 |
|---|---|
| Constructors / destructors / `operator+` | `[operator("create")]`, `[operator("delete")]`, `[operator("+")]` on normal methods |
| Multiple inheritance | One base class + interfaces |
| `struct` vs `class` differ only in default access | Same, plus `interface` as a third kind |
| `public:` labels grant access | Labels require matching inline modifiers |
| Free-function extension via ADL / helpers | `extends` extension methods |
| `friend` | Not available |
| `class X final` / C# `sealed` | `final class X` |
| `char b[4];` | `char[4] b;` |
| Uninitialized members | Everything is zero-initialized |
