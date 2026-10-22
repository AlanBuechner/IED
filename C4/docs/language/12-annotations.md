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
annotation   : qualified_name ( '(' ( expression (',' expression)* )? ')' )?
qualified_name: identifier ('::' identifier)*
```

- Arguments are expressions. The compiler's own annotations (section 2) take **string literals**; anything
  else is the error `'group' takes string literal arguments`. The arguments of every other annotation are
  **constant expressions** (D-42): numbers, bools, string literals (`"a" "b"` joins) and named constants
  (`Limits::kMax * 2`), folded where the declaration is written. A template's parameters are not constants
  there. An argument that does not fold is an error (`an annotation argument must be a constant number,
  bool or string`), and so is a wide string (`u"..."`: reflection stores UTF-8).
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

Any other annotation name is legal and has no compile-time effect. It is kept on the declaration and
registered with runtime reflection (D-42): its name as written and its folded arguments, on types, enums,
fields, methods, parameters, functions and variables, so libraries and tools define their own (a
serializer's `[Serialize]`, an editor's `[Range(0, 1)]`). See
[runtime/02-reflection.md](../runtime/02-reflection.md) section 3.3.

```c4
[Serialize, Range(-1, Limits::kMax * 2)]
struct Ship { [Tooltip("speed" "\n" "boost")] public float speed; }
```

A function declared and defined apart carries both lists, the declaration's first.

A near miss of a compiler annotation is a warning (D-42): `[operater("+")]` says `unknown annotation
'operater': did you mean 'operator'?`. It is still kept as a user annotation. A near miss is one edit from a
short compiler annotation (`test`, `link`, `uuid`, `group`) or two from a longer one, so `[Test]` warns and
`[Tag]` does not.

Ideas from older design notes such as `[Derive(...)]`, `[CTGen]` (compile-time code
generation) and `[Export]` were never confirmed as part of the language and are not planned.

---

## 4. Why annotations instead of keywords

The C4 rule is "one concept": a constructor, a destructor and `operator+` are all just functions
with an annotation, so they share one declaration form, one overload mechanism and one calling
mechanism (`x.operator_add(y)`). New hook points (`hash`, `begin`/`end`, `cast`) need no grammar
change, and libraries can define custom operators that generic code calls by name. Tests and
reflection groups follow the same pattern so that the core grammar stays small and C++-like.
