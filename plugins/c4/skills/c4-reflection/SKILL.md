---
name: c4-reflection
description: C4 runtime reflection in depth - the handle-based registry (types, variables, functions as first-class entities), [group("name")] annotations, registration emitted into every build, and the Bedrock Reflection namespace wrappers (Type/Field/Method/Param/EnumValue/Variable/Function/Group) with range-for views. Use for introspection tools, scripting bridges, hot reload; everyday code just needs include "bedrock.ch".
triggers:
  - "C4 reflection registry"
  - "Reflection::FindType wrapper API"
  - "[group] annotation groups"
  - "reflect_type reflect_field builtins"
  - "hot reload unload source tag"
  - "drill field type handle"
---

# C4 reflection (deep dive)

C4 has a runtime reflection system: types, variables and functions are first-class registry entities queryable by name or index. The compiler emits registration code for the program's declarations into **every build** (not just tests), so no opt-in compile flag is needed - you only need `include "bedrock.ch"` to use it from C4 source. Full reference: `docs/runtime/02-reflection.md` in the toolchain.

## Architecture
- Low level: a global registry implemented in C (C4Runtime/ReflectABI.h + ReflectRuntimeC.c). Everything is an opaque **handle** (`void*`); acquire one by name or index, then query the handle as much as you want - there is no repeated owner+index re-lookup. Name accessors return `const char*`.
- High level: the Bedrock wrappers in `Bedrock/Reflection/reflect.ch`, namespace `Reflection`. Every entity class (`Type`, `Field`, `Method`, `Param`, `EnumValue`, `Variable`, `Function`) is a thin handle wrapper with named accessors; member queries are handle-based, so you can drill type -> field -> the field's TYPE.
- **Groups** exist only where declared: tag any declaration with `[group("name")]` and it joins that group (no default groups). Acquire a group once by name; its handle is stable, so count/item read off it directly.

## Everyday usage (Bedrock wrappers)
    include "bedrock.ch"

    Reflection::Type t = Reflection::FindType("MyClass");   // also FindVariable / FindFunction
    if (!t.IsValid()) return;                               // missing name -> invalid handle
    printf("%s size=%llu fields=%lld\n", t.Name(), t.SizeOf(), t.FieldCount());

    for (Reflection::Field f : t) {                         // range-for over the type's FIELDS
        printf("  %s @%llu : %s\n", f.Name(), f.Offset(), f.TypeOf().Name());
    }
    for (Reflection::Method m : t.Methods()) {              // ...and METHODS via a view
        if (m.IsVirtual()) printf("virtual %s/%lld params\n", m.Name(), m.ParamCount());
    }
    for (Reflection::EnumValue e : t.Enums())               // ...and ENUM values
        printf("%s = %lld\n", e.Name(), e.Value());

Key `Type` accessors: `Name()`, `SizeOf()`, `AlignOf()`, `IsVirtualType()`, `BaseName()` (base class), `FieldCount()/FindField(name)/FieldAt(i)`, `MethodCount()/FindMethod(name)/MethodAt(i)`, `EnumCount()/EnumAt(i)`, `StaticVariableCount()/StaticVariableAt(i)`, and `FindByName(char*)` for registry lookup. Counts and indices are `int64`. A missing member yields an invalid handle - check `IsValid()`. `Field.IsConst()`, `Method.IsStatic()/IsConst()/ReturnTypeName()`, `Param.Name()/TypeName()`, `Variable.TypeName()/IsConst()/OwnerName()`, `Function.Symbol()/ParamAt(i)/IsStatic()/OwnerName()/ReturnTypeName()` read the rest.

Names: types register under qualified, argument-aware names (`Geo::Point`, `Geo::Box<int32>`), free functions and globals under qualified names (`Geo::Area`, `Type::s_count` for a static member), methods under their plain name. Each overload is its own `Function` (keyed by symbol); `FindFunction(name)` returns the first, `FunctionAt(i)` iterates all. There is no capacity limit.

Annotations: every entity has `AnnotationCount()/AnnotationAt(i)/FindAnnotation(name)` for the annotations the compiler does not act on (`[Range(0, 1)]`). An `Annotation`'s arguments fold at compile time: `ArgKind(i)` is `Int`, `Float`, `Bool`, `String` or `Type` - a type written as an argument (`[Requires(Render::MeshRenderer)]`, `[Holds(Box<int32>)]`), which the editor navigates and completes like code; its `StringArg(i)` is the type's qualified name and `TypeArg(i)` the registered type. Prefer a type to its name in a string: a typo is a compile error (in the editor too), and go-to-definition, completion and rename reach it. A name is a constant when it names one, else a type; `nameof(T)` is a string. A library declares the annotations it reads, with their arguments and where they go, at namespace scope (docs/language/12 section 4, D-87): `annotation Range(double min, double max) on field, parameter;`, `annotation Requires(typename component, typename... more) on type;`, `annotation System(const char*... groups) on type;`, `annotation Step(double by = 1.0) on field;`, `annotation Component on type;` (no parentheses when it takes nothing). Parameters are bool, an integer type, float/double, `const char*`, an enum or `typename` (a type); overloads, defaults and a trailing pack are allowed, and the places after `on` say where it goes (`type`, `enum`, `union`, `field`, `method`, `function`, `parameter`, `variable`; no `on`: anywhere). `annotation` and `on` are keywords only there: a variable named `annotation` is fine. `[annotation]` is an error. A use that fits no declaration is a compile error at the argument (`[Range] argument 2 ('max') is a double: "far" is a string`), a near miss of a declared name is an error, and an undeclared annotation is a warning. Reflection registers arguments as the declaration takes them (an integer for a double is a `Float`, defaults filled in), so a reader asks for one kind. The editor completes declared names inside `[...]` by place and the places after `on`, shows every overload in signature help and hover, and renames uses with the declaration. A use names an annotation as any name is named: from its namespace out to the global one, `[Scene::Range]` by its namespace, and only from a file that includes the declaring header; reflection registers a declared one by its qualified name. Declare the annotations your library's users write in the global namespace, in a header they include, so `[Range]` works anywhere; a name declared where a use does not see it is an error that says how to name it. A declaration is not a function: never call it.

Registry-level free functions: `TypeCount()`, `VariableCount()`, `FunctionCount()`, `GroupNameCount()` / `GroupNameAt(i)` (enumerate groups), and group access via `Reflection::Group g = Group("name");` then `g.Count()` / `g.Item(i)` / `g.KindOf(item)`.

## Low-level builtins
The wrappers are thin over the `reflect_*` builtin declarations (`reflect_type`, `reflect_field`, `reflect_method_param`, `reflect_variable_address`, `c4_reflect_unload_source`, ...). Use them only when wrapping in your own types or calling from C; otherwise stay on the Bedrock API.

## Hot reload
Registration is tagged: sources registered under a tag can be unloaded at runtime with `Reflection::UnloadSource(char* tag)` (builtin `c4_reflect_unload_source`), which drops that source's registry entries - the mechanism behind live-reloading game/script code while the process keeps running.

## Limits
- The registry is global runtime state; there is no per-module isolation beyond tags and groups.
