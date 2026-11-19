# 12 - Annotations

Annotations are C#-style attributes in square brackets placed before a declaration. C4 uses them
instead of new keywords for features that attach behavior to ordinary declarations: constructors,
destructors and operators (`[operator(...)]`), unit tests (`[test(...)]`), reflection groups,
foreign symbol binding and COM GUIDs.

See also: [06 - Lifecycle and operators](06-lifecycle-and-operators.md),
[runtime/03-testing.md](../runtime/03-testing.md), [runtime/02-reflection.md](../runtime/02-reflection.md),
[13 - Interop](13-interop.md).

---

## 1. Syntax

**Status:** **Implemented**

```c4
[operator("create")]
public void Init();

[operator("+", "add")]                       // one annotation, several arguments
public Vec2 Add(const Vec2& o);

[test("math.vector.addition")] [group("tests")]   // several bracket groups
public static void TestAddition();

[group("ui"), group("debug")]                // several annotations in one group
int32 g_Counter;
```

Grammar:

```text
annotations  : bracket_group+
bracket_group: '[' annotation (',' annotation)* ']'
annotation   : qualified_name ( '(' ( argument (',' argument)* )? ')' )?
argument     : expression | type
qualified_name: identifier ('::' identifier)*
```

- Arguments are expressions, or types. The compiler's own annotations (section 2) take **string literals**;
  anything else is the error `'group' takes string literal arguments`. The arguments of every other annotation
  are **constant expressions** (D-42): numbers, bools, string literals (`"a" "b"` joins), named constants
  (`Limits::kMax * 2`) and `nameof(T)` (a string), folded where the declaration is written - **or a type**:
  `[Requires(Render::MeshRenderer)]`, `[Holds(Box<int32>)]`, `[Points(int32*)]`. A name is a constant when it
  names one, else the type it names, looked up from the declaration's namespace and in the files its file
  includes, as a type written there would be; an alias is the type it names, and a template instance an
  annotation names is made. Reflection keeps a type as an argument of kind `Type` whose string is the type's
  qualified name ([runtime/02-reflection.md](../runtime/02-reflection.md) section 3.3), so the editor finds the
  type, completes it and renames it as anywhere in code. A template's parameters are neither constants nor
  types there. An argument that is neither is an error, said as the program's model is built, so an editor
  shows it too (`an annotation argument must be a constant number, bool or string, or a type: 'Render::Mesh'
  is neither a constant (...) nor a type (...)`), and so is a wide string (`u"..."`: reflection stores UTF-8).
- `[Name]` and `[Name()]` both have no arguments.
- Annotation names may be namespace-qualified (`[Game::Serialize]`); the compiler-recognized names
  below are unqualified.
- Placement: before objects (`class`/`struct`/`interface`), functions and methods (including
  extension methods and out-of-line definitions), variables and fields, parameters, enums, unions
  and aliases. For templates the annotations go before `template<...>`.

---

## 2. Annotations the compiler acts on

| Annotation | Applies to | Effect | Status |
|---|---|---|---|
| `[operator("name", ...)]` | methods, extension methods | binds the function to operators / lifecycle hooks (`create`, `delete`, `cast`, `begin`, `end`, `+`/`add`, ..., or a custom name). See [06](06-lifecycle-and-operators.md) | **Implemented** |
| `[test("group.path")]` | static free functions, static methods | registers a unit test. See [runtime/03-testing.md](../runtime/03-testing.md) | **Implemented** |
| `[test_setup]`, `[test_teardown]` | static free functions (global) or static methods (per class) | test fixtures | **Implemented** |
| `[group("name")]` | types, functions, variables | adds the entity to a named reflection group. See [runtime/02-reflection.md](../runtime/02-reflection.md) | **Implemented** |
| `[link("symbol")]` | function and variable declarations (a variable without an initializer), function definitions | a declaration binds to the foreign symbol of that name; a function definition is emitted under it. See [13](13-interop.md) | **Implemented** |
| `[target("avx2", ...)]` | functions and methods | builds the function for the program's processor with these features on as well (names a `-m` flag takes, [11 section 8](11-vectors-and-swizzling.md#8-the-simd-target)): its body uses them, and no caller built without them inlines it. Call it only where `Platform::HasProcessorFeature` says the processor has them | **Implemented** |
| `[uuid("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx")]` | types | attaches a COM GUID, read by `__uuidof(T)`; CBind emits it from `__declspec(uuid(...))`. A type without one gets a calculated one (the name-based UUID of its qualified name, [14](14-builtins.md)), so `__uuidof` works for every type | **Implemented** |

Details worth knowing:

- `[operator]` must be on a method inside the type or on an extension method (`... extends T`); a
  free function with `[operator]` and no `extends` is not an operator.
- Several `[operator]` names may share one function when their signatures fit; arity decides
  between unary and binary meanings (`"-"` is `negate` with no parameters, `subtract` with one;
  `"*"` with none is `dereference`), and every name in a multi-name `[operator(...)]` dispatches.
- A `[link]` variable has no initializer: it is the foreign variable, whose definition gives its value.
- `[target]` on any declaration of a function applies to it. A name that is no feature is an error, and so is a
  `[target]` function returning a vector wider than 16 bytes: Win64 returns one in YMM0 or ZMM0 only when the
  target has them, so the function and its callers would disagree (return it through a reference parameter).
  `simd_fma` works in a `[target("fma")]` function. A lambda inside it is built for the program's target.
- `[test("math.vector")]` on a class prefixes its methods' groups: `[test("addition")]` on a method is
  `math.vector.addition` (see [runtime/03-testing.md](../runtime/03-testing.md)).

---

## 3. All other annotations

**Status:** **Implemented**

Any other annotation name is legal and has no compile-time effect beyond its check (section 4.2). It is kept on the
declaration and registered with runtime reflection (D-42): its name (as written; a declared one's qualified name, section 4.2) and its
folded arguments, on types, enums,
fields, methods, parameters, functions and variables, so libraries and tools define their own (a
serializer's `[Serialize]`, an editor's `[Range(0, 1)]`). See
[runtime/02-reflection.md](../runtime/02-reflection.md) section 3.3.

```c4
[Serialize, Range(-1, Limits::kMax * 2)]
struct Ship { [Tooltip("speed" "\n" "boost")] public float speed; }

[Component, Requires(Render::MeshRenderer)]   // a type: the editor's go-to-definition and completion know it
struct MeshCollider { public bool convex; }
```

A function declared and defined apart carries both lists, the declaration's first.

A near miss of a compiler annotation is a warning (D-42): `[operater("+")]` says `unknown annotation
'operater': did you mean 'operator'?`. It is still kept as a user annotation. A near miss is one edit from a
short compiler annotation (`test`, `link`, `uuid`, `group`) or two from a longer one, so `[Test]` warns and
`[Tag]` does not; an edit is a character added, dropped or changed, or two neighbors swapped (`[tset]` is one edit
from `test`). A near miss of a *declared* annotation (section 4) is an error, and any other undeclared annotation a warning.

---

## 4. Declaring an annotation

**Status:** **Implemented** (D-87)

A library declares the annotations it reads, with the arguments each takes and where it goes, so a use is checked where
it is written - a misspelt name, a missing or extra argument, a string where a number belongs, an annotation on the
wrong kind of declaration - and the editor completes the name and shows the arguments as you type them. A library
declares the annotations every user writes in the global namespace, so a use anywhere names them alone; one declared
in a namespace is named as any of its names is (section 4.2).

```c4
/// [Component]: a type tools list as a component.
annotation Component on type;

/// [Requires(Render::MeshRenderer, ...)]: the components a component's entity cannot be without.
annotation Requires(typename component, typename... more) on type;

/// [Range(min, max)]: the inspector's slider bounds.
annotation Range(int64 min, int64 max) on field, parameter;
annotation Range(double min, double max) on field, parameter;

/// [Step(by)]: the inspector's step; 1 when written [Step].
annotation Step(double by = 1.0) on field;

/// [System] or [System("group", ...)]: the worlds a system is made in.
annotation System(const char*... groups) on type;

/// [Tag("name")]: anywhere.
annotation Tag(const char* name);

namespace Script { annotation Callable on function; } // [Script::Callable] outside namespace Script

[Component, Requires(Render::MeshRenderer)]          // fine
struct MeshCollider { [Range(0, 1)] public float friction; }

[Component, Requiers(Physics::CharacterMover)]        // error: unknown annotation 'Requiers': did you mean 'Requires'?
[Range(0, "far")] struct Bad { }                       // error: [Range] goes on a field or a parameter, not on a type;
                                                       //        and no [Range] takes a string for 'max'
```

### 4.1 The declaration

```
annotation_declaration := 'annotation' name [ '(' [ parameter { ',' parameter } ] ')' ] [ 'on' place { ',' place } ] ';'
```

- It is written at namespace scope; in a type's body it is an error (`[Tint] is declared at namespace scope, not in a
  type`). `annotation` and `on` are contextual: `annotation` starts a declaration only where a declaration starts and a
  name follows it, so a variable, field or parameter named `annotation` is still one. Its doc comment says what the
  annotation does.
- An annotation that takes no arguments may leave out the parentheses: `annotation Component on type;` and
  `annotation Component() on type;` are the same.
- **Overloads** are further declarations of the name with other parameters; a use takes the one its arguments fit
  (section 4.3).
- A parameter's type says what its argument is:

  | Parameter | Argument | Reflection registers |
  |---|---|---|
  | `bool` | `true`, `false` or a constant bool | `Bool` |
  | an integer type (`int8` ... `int64`, `uint8` ... `uint64`) | a constant integer the type holds | `Int` |
  | `float`, `double` | a constant number (an integer is converted) | `Float` |
  | `const char*` | a string literal or constant string (`nameof(T)`, `"a" "b"`) | `String` |
  | an enum type | one of its enumerators (`Pass::Glass`) | `Int`, the enumerator's value |
  | `typename` | a type (`Render::MeshRenderer`, `Box<int32>`, `int32*`) | `Type` |

  Anything else (`Core::String`, a struct, a pointer) is an error at the parameter, and so is C's `...`.
- A trailing **pack**, `T... name`, takes zero or more arguments of `T`: `typename... more`, `const char*... groups`.
- A parameter with a **default** (a constant of its type) may be left out; reflection registers the default, so a
  reader always finds every parameter (`[Step]` registers `Step(1.0)`). A default is followed only by defaults or a pack.
- `on` names **where** the annotation goes, each place once; without `on` it goes anywhere:

  | Place | Declarations |
  |---|---|
  | `type` | structs, classes, interfaces |
  | `enum`, `union` | enums, unions |
  | `field` | data members, static or not |
  | `method` | methods, extension methods included |
  | `function` | free functions |
  | `parameter` | parameters of functions and methods |
  | `variable` | variables at namespace scope |

  Each overload has its own places; a use takes only an overload that goes where it is written.
- An annotation belongs to the namespace it is declared in, and a use names it as section 4.2 says. The compiler's own
  annotations (section 2) cannot be declared.
- A declaration declares an annotation and nothing else: it is no function, type or variable, so `Scene::Range(0, 1)` is
  the error `'Scene::Range' declares the annotation [Range] ...: it is written in brackets on a declaration, not called`.
- `[annotation]` declares nothing: written anywhere, it is an error naming this form.

### 4.2 Uses

A use names a declared annotation as any name is named (D-87):

- `[Range]` is looked up from where it is written - the type it is on or in, then each namespace around it - out to the
  global namespace, and the first namespace that declares a `Range` the use sees has it; an inner one hides an outer one.
  `[Scene::Range]` names the one namespace `Scene` declares, from anywhere.
- A use sees the declarations of its own file and of the files it includes, as it sees their types.
- Reflection registers a declared annotation by its qualified name, however it is written: `[Range]` inside namespace
  `Scene` and `[Scene::Range]` outside it are both `Scene::Range`; one declared in the global namespace is its own name.

Every annotation of what reflection registers is checked (types, fields, enums, unions, globals, functions and their
parameters), as the program's model is built, so the editor shows each error where it is written:

- A **declared** annotation must fit one of its declarations: their places, their argument count and each argument's
  kind. The error names the declarations it was matched against.
- A name a declaration has but the use does not see is an error saying why: `annotation [Range] is not visible here:
  Scene::Range is declared in namespace Scene: write [Scene::Range]`, or `... it is declared in scene.ch, which this file
  does not include`.
- An **undeclared** name that is a near miss of one the use sees (one edit from a name shorter than 6 characters, two
  from a longer one, case counting; two swapped neighbors are one edit, so `[Rnage]` and `[Setp]` are near misses of
  `Range` and `Step`) is an error naming the one it misspells. Any other undeclared annotation is a
  **warning** (`annotation [Serialize] is not declared: declare it, what it takes and where it goes, with
  `annotation Serialize(...) on ...;``) and is kept for reflection as written (section 3), as before.

### 4.3 Choosing an overload

A use's arguments are folded (section 1), then matched against each overload that goes where the use is written:

1. Each argument fits its parameter as the table in 4.1 says; an argument for a pack fits the pack's type.
2. Of the overloads that fit, the one with the fewest conversions wins (an integer for a `double` is one), then one
   without a pack over one with. Two left that tie is the error `[Range(0, 1)] is ambiguous: ...`.

Reflection registers the arguments as the chosen overload's parameters take them: `[Range(0, 1)]` against only
`Range(double min, double max)` registers two `Float`s, so a reader asks for one kind.

### 4.4 In the editor

C4LSP knows a declared annotation as code: completion inside `[...]` offers the declared annotations the caret sees
that go where it is (and the compiler's own), after `[Scene::` those of namespace `Scene`, and after `on` the places; signature help inside its parentheses shows every overload
with the current argument; hover on a use or a declaration shows its declarations and doc comment; go-to-definition
leads to the overload whose parameters the use's written arguments fit; find-references and rename of any of its
declarations reach every declaration and use; and its declarations are in the outline and workspace symbols, `annotation`,
`on` and the places colored as keywords and the name as its uses are.

Ideas from older design notes such as `[Derive(...)]`, `[CTGen]` (compile-time code
generation) and `[Export]` were never confirmed as part of the language and are not planned.

---

## 5. Why annotations instead of keywords

The C4 rule is "one concept": a constructor, a destructor and `operator+` are all just functions
with an annotation, so they share one declaration form, one overload mechanism and one calling
mechanism (`x.operator_add(y)`). New hook points (`hash`, `begin`/`end`, `cast`) need no grammar
change, and libraries can define custom operators that generic code calls by name. Tests and
reflection groups follow the same pattern so that the core grammar stays small and C++-like.

Declaring an annotation is the one keyword (`annotation Range(double min, double max) on field;`, section 4): what it
declares is no function, type or variable - it is never called, emitted or reflected - so it is not written as one
marked by an annotation, and where it goes is part of what it declares, said after `on` as a use's place is named.
