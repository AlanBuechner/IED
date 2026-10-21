# Runtime reflection

C4 has built-in runtime reflection: types, global variables and functions are entities in a registry
that a program can query by name or index at run time. Reflection is on for every build - there is no
opt-in flag - and is the base for serialization, tooling, scripting bridges and hot reload.

Sources:
- Compiler emission: `C4Lib/src/Backend/Reflection/RegistrationEmitter.cpp` (what is registered) and
  `ReflectionTables.cpp` (the wire format as LLVM constants); type names from
  `C4Lib/src/Model/Types/QualifiedName.cpp`; invoke thunks from `C4Lib/src/Backend/HIR/InvokeThunk.cpp`.
- Runtime registry: `C4Runtime/ReflectABI.h` (the wire format, included by the generated file),
  `C4Runtime/ReflectRuntimeC.c` (the registry: registration, unload, lookups, groups), `ReflectHandlesC.c`
  (what a handle tells, invoke), and `Reflect/` (owned record copies, hash indexes, groups). Pure C, linked into
  every exe.
- C4 wrapper API: `Bedrock/Reflection/reflect.ch` (namespace `Reflection`).

---

## 1. How registration works

The compiler emits the program's tables into its own module: constant rows in the layout of
`ReflectABI.h`, gathered in one `c4_reflect_image` (`__c4_reflect_image`), and a static constructor,
`__c4_reflect_init`, that calls `c4_reflect_register_image(tag, &image)` before every other static
constructor, so the program's global starts can already use reflection. The runtime registers the whole image
under one hold of its lock. A script's loader registers its image the same way
([scripting](04-scripting.md)). An annotation argument that does not fold is a compile error.

What is registered:

| Entity | Identity | Recorded data |
|---|---|---|
| Object types (class/struct/interface) | the qualified, argument-aware name: `Geo::Point`, `Geo::Box<int32>`, `Outer::Inner` | size, align, non-static fields `{name, type name, size, offset, isConst}`, methods `{name, params {name, type}, return type, isStatic, isVirtual, isConst, invoke thunk}`, primary base name, `[group]` names. Pure packed SIMD vector types are skipped. |
| Enums | the qualified name | size, align, `{name, int64 value}` per enumerator |
| Global variables and static data members | the emitted symbol; found by the qualified name (`Ns::g_hp`, `Type::s_count`) | type name, size, align, isConst, owner type of a static member, groups, and the variable's **live address** |
| Free functions and methods (every template-instantiated method too) | the emitted symbol, so each overload is its own entry; found by the qualified name of a free function (`Ns::Add`) or the plain name of a method | return type, params `{name, type}`, isStatic, isVariadic, owner type, groups, invoke thunk |

Type names in fields, parameters and returns are the same qualified names, so they resolve to the
registered type. Every entity, and every field and parameter, also carries its **annotations** (section
3.3). `isVirtual` is true for every method a call dispatches through a vtable: one written
`virtual`, an `override`, an interface method and its implementations. The compiler's builtins (`printf`,
`memcpy`, the `reflect_*` accessors), the runtime's `c4_` functions and the lifecycle functions the compiler
writes (`__c4_delete` for a type that needs one, BUG-238) are not the program's and are not registered.

Every registration carries a **source tag** (`-reflect-tag <tag>` on the C4.exe command line; empty
for the main executable). The tag is the unit of hot reload.

---

## 2. Registry model

- **Handles.** Every entity is an opaque handle (`void*` on the C4 side, `const c4_*_info*` in C).
  Acquire a handle once by name or index, then query it as often as needed. `reflect_kind_of(handle)`
  says what kind of entity a handle is (object, enum, union, variable, function; `Reflection::EntityKind`).
- **Layouts.** A named union registers as a union whose fields share its storage. A class's anonymous union
  lists each view's fields at their offsets, and a packed region lists one field per lane name (one element
  wide) plus its whole-lane array (`data`, typed `float[3]`). Packed vector classes (`Math::FVector3`) are
  registered like any class. **Aliases:** within one union only the first view (or the first plain member)
  is a field of its own; every later view, a lane's second name (`r` beside `x`) and the whole-lane array
  name bytes already named, and `Field.IsAlias()` is true for them. The serializers skip aliases, so declare
  the view that should be written first.
- **Stable records.** The runtime deep-copies the emitted tables and every string into owned records,
  so handles stay valid when the same identity registers again (the entry is refilled in place) and
  the tables may live in an image that is later unloaded.
- **Capacity.** Storage grows on demand; types, variables and functions are found through hash indexes
  by name (the first registered overload for a function name). Iterate by index to see every overload.
- **Lazy type resolution.** A field, parameter, return or variable type resolves to another type
  handle on demand. Primitive types are not registered, so their type handle is null; use the type
  name string instead.
- **Groups.** An entity is in a group only if its declaration carries `[group("name")]`. There are no
  default groups. A group handle is stable, so count and items read off it directly.
- **Hot reload.** `c4_reflect_unload_source(tag)` (Bedrock: `Reflection::UnloadSource(tag)`)
  first calls the tag's unload listeners, while its entities are still registered, then removes every
  type, variable and function registered under that tag, plus their group memberships, and returns how
  many entries were removed. The executable's own tag `""` cannot be unloaded. Registering the same
  identities again (the reloaded image) brings them back.
- **Several tags, one identity.** Registering an identity under one tag while another tag holds it keeps the
  older registration behind the new one (`C4Runtime/Reflect/ReflectShadows.c`): the new one is what lookups
  and handles see; unloading its tag brings the one it hid back; unloading a tag whose registration waits
  behind removes only that one. Registering again under the same tag replaces its own registration in place.
  So two scripts that make the same template instance keep each other's entry, and a script never takes away
  a type its host registered. `UnloadSource` counts the entries the tag held in front.
- **Unload listeners.** `Reflection::OnUnload(tag, listener, user)` registers
  `funcptr<void(const char* tag, void* user)>` for one tag, or for every tag with `""`, and returns an id
  for `Reflection::RemoveUnloadListener(id)`. A listener may add or remove listeners; it is the place to
  drop handles and objects of the unloading source.
- **Unloading in two steps.** The script host unloads a script's registrations around its exits:
  `c4_reflect_notify_unload(tag)` runs the tag's listeners first (the script's globals are still alive), and
  `c4_reflect_remove_source(tag)`, after the exits, removes the entries without calling the listeners again;
  `c4_reflect_unload_source` is the two together.
- **Which source.** `Type::Tag()`, `Variable::Tag()` and `Function::Tag()` name the tag an entity was registered
  under (`""` for the executable's own); `Scripting::Script::FindType` and `FindFunction` use it to find a
  script's own entities.

```c4
[group("components")]
struct Health { public int32 hp; public int32 max; };

[group("components")]
int32 g_score = 7;

[group("SubSystem")]
int32 Add(int32 a, int32 b) { return a + b; }
```

---

## 3. Bedrock `Reflection` API

`include "bedrock.ch"` and use the wrappers. Each class is a thin handle with named accessors; a
missing name or index gives an invalid handle, so check `IsValid()`.

| Class | Accessors |
|---|---|
| `Type` | `IsValid, Name, Tag, Kind, SizeOf, AlignOf, IsVirtualType, BaseName, FieldCount, FindField(name), FieldAt(i), MethodCount, FindMethod(name), MethodAt(i), EnumCount, EnumAt(i), FindByName(name)`; range-for over fields; `Methods()` and `Enums()` views for range-for |
| `Field` | `IsValid, Name, Offset, SizeOf, TypeName, TypeOf, IsConst, IsAlias, AddressIn(object), Get(object, out), Set(object, value)` |
| `Annotation` | `IsValid, Name, ArgCount, ArgKind(i)` (an `AnnotationArg`: `Int`, `Float`, `Bool`, `String`)`, IntArg(i), FloatArg(i), BoolArg(i), StringArg(i)` |
| `Method` | `IsValid, Name, ParamCount, IsVirtual, IsStatic, IsConst, ParamAt(i), ReturnTypeName, ReturnType, IsInvokable, Invoke(object, result, arguments), Call(object, result, args...)` |
| `Param` | `IsValid, Name, TypeName, TypeOf` |
| `EnumValue` | `IsValid, Name, Value` |
| `Variable` | `IsValid, Name, Tag, TypeName, IsConst, OwnerName, TypeOf, Address` |
| `Function` | `IsValid, Name, Tag, Symbol, ParamCount, ParamAt(i), IsStatic, OwnerName, ReturnTypeName, ReturnType, IsInvokable, Invoke(result, arguments), InvokeOn(object, result, arguments), Call(result, args...), CallOn(object, result, args...)` |
| `Group` | constructed from a name; `IsValid, Count, Item(i), KindOf(item)` (an `EntityKind`) |

`Type` also has `StaticVariableCount` and `StaticVariableAt(i)` for its static data members. `Type`, `Field`,
`Method`, `Param`, `Variable` and `Function` all have `AnnotationCount`, `AnnotationAt(i)` and
`FindAnnotation(name)`.

Free functions in `Reflection`: `FindType`, `FindVariable`, `FindFunction`, `TypeAt`, `VariableAt`,
`FunctionAt`, `TypeCount`, `VariableCount`, `FunctionCount`,
`GroupNameCount`, `GroupNameAt`, `UnloadSource`, `OnUnload`,
`RemoveUnloadListener`. Counts and indices are `int64`, as everywhere in Bedrock.

Example (from `Bedrock/Tests/reflection_tests.cf`):

```c4
include "bedrock.ch"

struct Point2D { public int32 x; public int32 y; }
enum class Traffic { Stop, Slow, Go }

void Dump() {
	Reflection::Type t = Reflection::FindType("Point2D");
	if (!t.IsValid()) return;
	printf("%s size=%llu fields=%lld\n", t.Name(), t.SizeOf(), t.FieldCount());
	for (Reflection::Field f : t) {
		printf("  %s @%llu : %s\n", f.Name(), f.Offset(), f.TypeName());
	}
	Reflection::Type e = Reflection::FindType("Traffic");
	for (Reflection::EnumValue v : e.Enums()) {
		printf("%s = %lld\n", v.Name(), v.Value());
	}
}
```

### 3.1 Fields on an instance

`Field.AddressIn(object)` is the field inside `object`. `Get(object, out)` copies the field into `out` and
`Set(object, value)` assigns `value` to it, both through the type's own copy (a `Core::String` field gets its own
buffer). Both return false for an invalid handle, a null object or a `T` whose size is not the field's;
`Set` also refuses a const field. The size check is the only type check, so `T` must be the field's type.

```c4
struct Item { public int32 id; public Core::String label; }
Item item;
Reflection::Type t = Reflection::FindType("Item");
t.FindField("id").Set(&item, 12);
Core::String label;
t.FindField("label").Get(&item, label);
```

### 3.2 Calling functions and methods

Every registered function that has code in the program gets an **invoke thunk**,
`void(const void* code, void* object, void* result, void** arguments)`, and its table entry stores the thunk's
address:

- a function or method with a body (a template-made one when the program uses it, as D-16 compiles it);
- every virtual method, body or not: the thunk dispatches through the object, so a pure interface method
  is callable and runs the object's implementation.

One thunk serves every function of a signature: it calls the `code` it is given, which the entry stores beside the
thunk (the runtime passes it). The signature is what the thunk depends on: how `this` is passed, `noexcept`, and the
return and parameter types as they are passed - every pointer alike, every reference alike, an integer or an enum by
its width, an object (whose copy and delete run in the thunk) by its own type. A virtual method and a foreign
(`[link]`) function have a thunk of their own, which ignores `code` (the entry's is null). The IED server's 4,866
thunks became 1,038 this way.

There is no thunk (`IsInvokable()` is false) for a C variadic function, a function local to a body, an
extension method of a primitive type, a function declared but not defined in the program (an external
declaration), or one whose by-value parameter type cannot be copied.

The arguments:

- `object` is the receiver of a method: the address of an instance of the method's owner type. For an
  interface method that is the interface sub-object (`(void*)(IShape*)&circle`). It is unused for a free or
  static function and may be null.
- `arguments[i]` is the address of an object of exactly parameter i's type; nothing is converted. A
  by-value parameter receives a copy of it, a reference parameter binds that object itself.
- `result` is storage for the return value, which is built there without destroying what the storage held,
  so pass uninitialized or zero storage (`Core::String s;` is zero). A reference result stores the referent's
  address there (`int32* p; f.Call(&p);`). For a void function it is unused and may be null.

`Invoke` and `InvokeOn` return false when there is no thunk, the handle is invalid, or an instance method
gets no object. `Call` and `CallOn` take the arguments themselves and also return false when their count
is not the parameter count; each must already have its parameter's type (`Call(&r, 2, 3)` for two `int32`
parameters, `(int64)2` for an `int64` one). Exceptions leave the thunk like any call.

```c4
int32 Add(int32 a, int32 b) { return a + b; }

int32 r = 0;
Reflection::Function add = Reflection::FindFunction("Add");
add.Call(&r, 2, 3);                          // r == 5
int32 a = 40; int32 b = 2;
void*[2] args = { (void*)&a, (void*)&b };
add.Invoke(&r, &args[0]);                    // r == 42

Bird bird;
Reflection::FindType("Animal").FindMethod("Legs").Call(&bird, &r);   // dispatches to Bird::Legs
```

### 3.3 Annotations

Every annotation the compiler does not act on (D-42, [12 - Annotations](../language/12-annotations.md)) is
registered with its entity: types, enums, fields, methods, their parameters, functions and variables. An
annotation keeps its name as written (`Game::Serialize`) and its arguments folded at compile time, each an
integer, float, bool or string. The compiler's own annotations (`operator`, `test`, `group`, ...) are not
listed; groups have their own API.

```c4
[Serialize, Range(-1, Limits::kMax * 2)]
struct Ship {
	[Range(0.0, 1.0)] public float speed;
	[Command("go", true)] public void Go([Units("m")] float distance) { }
}

Reflection::Type ship = Reflection::FindType("Ship");
Reflection::Annotation range = ship.FindAnnotation("Range");
int64 low = range.IntArg(0);                                     // -1
double top = ship.FindField("speed").FindAnnotation("Range").FloatArg(1);
const char* units = ship.FindMethod("Go").ParamAt(0).FindAnnotation("Units").StringArg(0);
```

A function declared and defined apart lists the declaration's annotations, then the definition's.

The wrappers sit on about 85 low-level `reflect_*` builtins (`reflect_type`, `reflect_field`,
`reflect_method_param`, `reflect_variable_address`, `reflect_group`, ...). Use those only when writing
your own wrapper or calling from C. Name accessors return `const char*`.


---

## 4. Capability matrix

| Want | Status |
|---|---|
| Find a type, variable or function by name or index | **Implemented** |
| Type name, size, align, base name, is-virtual, fields, methods, enum values | **Implemented** |
| Field name, offset, size, type name, type handle, isConst | **Implemented** |
| Method name, parameters (names and types), return type, isVirtual, isStatic, isConst | **Implemented** |
| Function return type, parameters, owner, isStatic from the wrapper | **Implemented** |
| Read/write a global or static | **Implemented** through `Variable.Address()` |
| Read/write a field on an instance | **Implemented** - `Field.Get` / `Set` / `AddressIn` (section 3.1) |
| Invoke a function or method | **Implemented** - invoke thunks (section 3.2); free, static, instance and virtual |
| List a type's static functions and variables | **Implemented** - static methods are in its method table (`IsStatic`), static data members through `StaticVariableAt` |
| Template instantiations | **Implemented** - each registers under its argument-aware name (`Box<int32>`, `Box<Geo::Point>`) |
| Namespaced types, enums and functions | **Implemented** - qualified names |
| Overloaded functions | **Implemented** - one entry per overload, keyed by symbol |
| Operators, virtuals, inheritance | **Partial** - operators appear as their method names, `IsVirtual` for every dispatched method, single primary base name only |
| Groups | **Implemented** |
| Annotations on types, enums, fields, methods, parameters, functions, variables | **Implemented** (section 3.3) |
| Unload by tag | **Implemented** |
| One identity under several tags (the latest in front, the earlier ones kept behind it) | **Implemented** - IsolatedTest `Reflection/StackedRegistrations` |
| Reload callbacks | **Implemented** - unload listeners per tag |
| Script loader | **Implemented** - B30, [scripting](04-scripting.md): a script registers under its tag at load (ScriptTest `Reflection`, `Reload`) |
| Unions | **Implemented** - a named union registers as `EntityKind::Union`; anonymous-union views and packed lanes are fields; later views are aliases (section 2) |
| Packed vectors | **Implemented** - registered like any class, one field per lane |
| Capacity | **Implemented** - growable storage, hash-indexed lookups |
| Thread safety | **Implemented** - a reader-writer lock: lookups, counts, iteration and groups share it; registration and unload take it alone; unload listeners run without it. Reading a handle takes no lock, so a source must not be unloaded or re-registered while other threads still read its handles |

Tests: Bedrock `reflection_layout_tests.cf` (named and namespaced unions, class views, packed lanes and
vectors, aliases, union groups), `reflection_tests.cf` (iteration, qualified and template names, overloads, parameter
names, flags, virtual methods, static members, field access, local enums), `reflection_invoke_tests.cf`
(free, static, instance, virtual and interface calls; reference, struct and object results; reference and
object arguments; refused calls), `reflection_annotation_tests.cf` (every entity kind, argument kinds,
qualified names, compiler annotations left out, declaration and definition), the serializer and DOM tests
that walk fields, and IsolatedTests `Reflection/ManyTypes` (1100 types, globals and functions),
`Reflection/DistinctSymbols`, `Reflection/UnloadAndReregister` (listeners, unload, register again).

---

## 5. Intended direction

LongTermPlan.txt item 4 asks for a complete reflection system that can collect full information on
properties, functions and types, and also **modify properties and invoke functions**:

- query a type's static functions and static variables;
- get reflection information for every parameter and function;
- type -> function -> info about the function -> **invoke** it;
- type -> property -> info about the property -> **read/write** it;
- work with operator overloads, virtuals, inheritance and templates;
- rework Bedrock's abstractions on top of it.

The query side is largely done. Still needed, in rough order:

1. A loader for scripting (ties into ORCv2 scripting, LongTermPlan item 6). Scripts
   are meant to share one registry with compiled code.
