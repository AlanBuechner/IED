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

Registry-level free functions: `TypeCount()`, `VariableCount()`, `FunctionCount()`, `GroupNameCount()` / `GroupNameAt(i)` (enumerate groups), and group access via `Reflection::Group g = Group("name");` then `g.Count()` / `g.Item(i)` / `g.KindOf(item)`.

## Low-level builtins
The wrappers are thin over the `reflect_*` builtin declarations (`reflect_type`, `reflect_field`, `reflect_method_param`, `reflect_variable_address`, `c4_reflect_unload_source`, ...). Use them only when wrapping in your own types or calling from C; otherwise stay on the Bedrock API.

## Hot reload
Registration is tagged: sources registered under a tag can be unloaded at runtime with `Reflection::UnloadSource(char* tag)` (builtin `c4_reflect_unload_source`), which drops that source's registry entries - the mechanism behind live-reloading game/script code while the process keeps running.

## Limits
- The registry is global runtime state; there is no per-module isolation beyond tags and groups.
