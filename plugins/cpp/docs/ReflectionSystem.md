# Reflection System

The C++ Reflection System (IED's C/C++ plugin, `plugins/cpp/libraries/`) provides **compile-time metadata extraction** and **runtime type introspection** for C++ classes. It enables serialization, editor integration, dynamic function invocation, and ECS component inspection — all without manual boilerplate or runtime AST parsing.

The system is a three-stage pipeline:

```
Source Headers (.h/.cpp)
        │
        ▼  [1] HeaderTool (Clang AST consumer)
   .reflected files (YAML)
        │
        ▼  [2] CodeGen (C++ code generator)
   generated/generated.cpp
        │
        ▼  [3] Compiler + Linker
   Runtime Reflect::Registry with full type metadata
```

---

## Table of Contents

- [Architecture Overview](#architecture-overview)
- [Stage 1 — Header Parsing (HeaderTool)](#stage-1--header-parsing-headertool)
- [Stage 2 — Static Reflection Data](#stage-2--static-reflection-data)
- [Stage 3 — Code Generation (CodeGen)](#stage-3--code-generation-codegen)
- [Runtime Reflection API](#runtime-reflection-api)
- [ObjectDescription Serialization](#objectdescription-serialization)
- [Build Pipeline Integration](#build-pipeline-integration)
- [Usage Guide](#usage-guide)

---

## Architecture Overview

The reflection system spans four projects under `plugins/cpp/libraries/`:

| Project | Location | Role |
|---------|----------|------|
| **HeaderTool** | `HeaderTool/` | Clang-based AST consumer that parses source headers and extracts `__attribute__((annotate(...)))` markers into `.reflected` YAML files |
| **StaticReflection** | `StaticReflection/` | Shared data structures (`Reflection::Class`, `Reflection::Property`, `Reflection::Function`) used by both HeaderTool and CodeGen to represent reflection metadata in a language-neutral format |
| **CodeGen** | `CodeGen/` | Reads `.reflected` YAML files, generates `generated/generated.cpp` containing runtime registration code that populates the `Reflect::Registry` |
| **Reflection** | `Reflection/` | The runtime reflection library — provides `Reflect::Class`, `Reflect::Property`, `Reflect::Function`, `Reflect::Registry`, and the `REFLECTED_BODY` / `CLASS` / `PROPERTY` / `FUNCTION` macros |

A fifth project, **ObjectDescription** (`ObjectDescription/`), builds on top of reflection to provide generic serialization and deserialization of reflected objects.

---

## Stage 1 — Header Parsing (HeaderTool)

### Purpose

The HeaderTool is a **Clang-based AST consumer** that scans C++ source files for classes, properties, and functions annotated with Clang's `__attribute__((annotate(...)))` mechanism. It extracts structured metadata and writes it to intermediate `.reflected` YAML files in the project's `generated/` folder.

### Entry Point

The tool is invoked as a standalone executable. Its `main()` (in `Source.cpp`) uses Clang's `CommonOptionsParser` and `ClangTool` infrastructure:

```cpp
int main(int argc, char** argv) {
    ReflectionConsumer::Init();
    auto ExpectedParser = CommonOptionsParser::create(argc, ...);
    ClangTool Tool(ExpectedParser->getCompilations(), ...);
    return Tool.run(newFrontendActionFactory<ReflectionAction>().get());
}
```

### AST Consumption

`ReflectionConsumer` implements `clang::ASTConsumer` and handles each translation unit:

1. **Class Discovery**: Uses `ASTMatchFinder` with a `cxxRecordDecl` matcher to find all C++ class declarations in the translation unit.
2. **Attribute Extraction**: For each class, extracts `__attribute__((annotate("reflect-class,...")))` strings using `GetAnnotations()`, which strips the Clang formatting wrapper (`__attribute__((annotate("...")))`).
3. **Source Filtering**: Only processes classes from headers located within the project's `src/` folder (via `is_subpath()` check), ignoring third-party and system headers.
4. **Property Collection**: Iterates class fields, looking for `reflect-property` annotations. Records field name, desugared type string, and attribute data.
5. **Function Collection**: Iterates class methods, looking for `reflect-function` annotations. Records method name, parameter types (desugared, non-reference), and attribute data.

### Attribute String Format

The annotation strings use a simple comma-separated format supporting two kinds of attributes:

- **Flags**: bare identifiers, e.g., `GROUP=Components`
- **Properties**: key=value pairs, e.g., `GROUP=Components`

Example annotation string: `reflect-class,GROUP=Components`

Parsing is handled by `ParseAttributeString()` in `ReflectionData.cpp`, which tokenizes on `,` and `=` delimiters.

### Output

For each source file containing reflected classes, a `.reflected` YAML file is written to `generated/<filename>.reflected`. The YAML structure mirrors the `Reflection::File` data model:

```yaml
Header: "src/Engine/Scene/TransformComponent.h"
Classes:
  - Name: "Engine::TransformComponent"
    Attributes: "GROUP=Components"
    Props:
      - Name: "m_Position"
        Type: "Math::Vector3"
        Attributes: ""
    Functions: []
```

---

## Stage 2 — Static Reflection Data

The `StaticReflection` library provides the intermediate data model shared between HeaderTool (writer) and CodeGen (reader).

### Data Model

All types live in the `Reflection` namespace (`StaticReflection/ReflectionData.h`):

#### `Reflection::Attribute`

```cpp
struct Attribute {
    enum Type { Flag, Prop, } type;
    Core::String name;
    Core::String value;
};
```

Represents a single attribute — either a bare flag or a key-value property.

#### `Reflection::Property`

```cpp
class Property {
public:
    Core::String m_Name;          // Field name
    Core::String m_Type;          // Fully-qualified C++ type string
    Core::Vector<Attribute> m_Attributes;
};
```

Represents a reflected class member variable. The `m_Type` stores the desugared, fully-qualified type name as resolved by Clang (e.g., `Math::Vector3`, `Core::String`).

#### `Reflection::Function`

```cpp
class Function {
public:
    Core::String m_Name;                    // Method name
    Core::Vector<Core::String> m_Types;     // Parameter type strings
    Core::Vector<Attribute> m_Attributes;
};
```

Represents a reflected class method. Parameter types are stored as fully-qualified, non-reference C++ type strings.

#### `Reflection::Class`

```cpp
class Class {
public:
    Core::String m_Name;                    // Short name (last namespace segment)
    Core::String m_Group;                   // GROUP attribute value
    Core::String m_SemanticName;            // Fully-qualified name (e.g. "Engine::TransformComponent")
    Core::Vector<Attribute> m_Attributes;
    Core::Vector<Property> m_Props;
    Core::Vector<Function> m_Functions;
};
```

Represents a reflected C++ class. `m_Name` is extracted from the last segment of `m_SemanticName` (after the final `::`). `m_Group` is read from the `GROUP=<value>` attribute if present.

#### `Reflection::File`

```cpp
class File {
public:
    Core::String m_Header;                  // Source header path
    Core::Vector<Class> m_Classes;
};
```

Represents a single source file and its reflected classes. Supports `Save()` (YAML emission) and `Load()` (YAML parsing) via the ObjectDescription system.

#### `Reflection::ReflectionData`

```cpp
class ReflectionData {
public:
    Core::Vector<File> m_Files;
    Core::Vector<Core::String> m_Headers;   // Collected header paths
    Core::Vector<Class> m_Classes;          // Collected classes across all files

    void Collect();                         // Flatten files into unified lists
    void LoadFolder(fs::path folder);       // Load all .reflected files from a directory
};
```

Top-level aggregate used by CodeGen. `LoadFolder()` iterates the `generated/` directory for `.reflected` files, and `Collect()` flattens per-file data into unified header and class lists.

---

## Stage 3 — Code Generation (CodeGen)

### Purpose

The CodeGen executable reads all `.reflected` YAML files from the project's `generated/` folder and produces `generated/generated.cpp` — a C++ source file that, when compiled and linked, populates the runtime `Reflect::Registry` with complete type metadata.

### Entry Point (`Source.cpp`)

```cpp
int main(int argc, char** argv) {
    std::string projectName = ...; // Derived from current directory name
    Validate();                    // Remove stale .reflected files
    Reflection::ReflectionData data;
    data.LoadFolder("generated");
    data.Collect();
    WriteCode("generated/generated.cpp", projectName, data);
}
```

### Validation

Before generating code, `Validate()` walks the `src/` directory to build a set of known filenames, then removes any `.reflected` files in `generated/` whose stem does not correspond to an existing source file. This prevents stale reflection data from deleted headers.

### Generated Code Structure

The output `generated/generated.cpp` contains three sections:

#### 1. Header Includes and Utilities

```cpp
#include "Reflection.h"
#include <stdarg.h>
#include "src/Engine/Scene/TransformComponent.h"   // Each reflected header
// ... more includes ...
#define OFFSET_OF(s,m) ((size_t)&(((s*)0)->m))
```

#### 2. Generator Helper Class

A class named `<ProjectName>Generator` is generated with static helper methods:

- **Function wrappers**: For each reflected method, a `static void <ClassName><MethodName>(void* obj, std::vector<void*> args)` that casts the `void*` object pointer and argument pointers to their real types and invokes the actual method.
- **Offset helpers**: For each reflected property, a `static uint64_t <ClassName><PropertyName>()` returning `OFFSET_OF(ClassName, PropertyName)`.

Example generated code:

```cpp
class EngineGenerator {
public:
    static void TransformComponentUpdate(void* obj, std::vector<void*> args) {
        float* v0 = (float*)args[0];
        ((Engine::TransformComponent*)obj)->Update(*v0);
    }
    static uint64_t TransformComponentm_Position() {
        return OFFSET_OF(Engine::TransformComponent, m_Position);
    }
};
```

#### 3. Registry Registration

For each reflected class, a `static Reflect::Registry::Add<T>` instance is created at file scope. This registers the class during static initialization (before `main()`):

```cpp
static Reflect::Registry::Add<Engine::TransformComponent> ClassTransformComponent(
    "TransformComponent", "Engine::TransformComponent", "Components",
    {},                          // flags
    {},                          // attributes
    {                             // properties
        Reflect::Property("m_Position",
            typeid(Math::Vector3).hash_code(),
            sizeof(Math::Vector3),
            EngineGenerator::TransformComponentm_Position(),
            {},                   // property flags
            {}                    // property attributes
        )
    },
    {}                            // functions
);

void DeadLinkEngine() {}          // Linker dead-code elimination guard
```

The `Registry::Add<T>` constructor is called at static initialization time, which calls `Registry::GetRegistry()->AddClass(...)` — ensuring all reflected types are registered before the application starts.

### Linking Guard

The `LINK_REFLECTION_DATA` macro (defined in `Reflection.h`) creates a linker reference to the `DeadLink<project>()` function, preventing the linker from discarding the generated registration code due to dead-code elimination:

```cpp
#define LINK_REFLECTION_DATA \
    void REFLECT_CAT(DeadLink, PROJECT_NAME)(); \
    void* REFLECT_CAT(_DeadLink, PROJECT_NAME) = (void*)&REFLECT_CAT(DeadLink, PROJECT_NAME);
```

This macro must be placed in at least one `.cpp` file per project that uses reflection.

---

## Runtime Reflection API

The runtime library (`Reflection/src/`) provides the `Reflect` namespace with classes for querying type metadata at runtime.

### `Reflect::Class`

Represents a registered reflected type:

| Member | Description |
|--------|-------------|
| `GetName()` | Short class name (e.g., `"TransformComponent"`) |
| `GetSname()` | Semantic/fully-qualified name (e.g., `"Engine::TransformComponent"`) |
| `GetTypeID()` | `typeid(T).hash_code()` — unique per type |
| `GetSize()` | `sizeof(T)` |
| `GetGroup()` | User-defined group string from `GROUP=<value>` attribute |
| `CreateInstance(void*)` / `DestroyInstance(void*, bool)` | Allocate/deallocate instances through stored function pointers |
| `HasFlag(const std::string&)` | Check for a class-level flag attribute |
| `GetProperties()` | Returns all reflected properties |
| `GetProperty(const std::string&)` | Lookup property by name |
| `GetFunctions()` | Returns all reflected functions |
| `GetFunction(const std::string&)` | Lookup function by name |

### `Reflect::Property`

Represents a reflected member variable:

| Member | Description |
|--------|-------------|
| `GetName()` | Property name |
| `GetTypeID()` | Type ID of the property's type (`typeid(T).hash_code()`) |
| `GetSize()` | `sizeof(property_type)` |
| `GetOffset()` | Byte offset of the property within its parent class (computed at compile time via `OFFSET_OF`) |
| `HasFlag(const std::string&)` | Check for a property-level flag |
| `GetAttributes()` / `HasAttribute()` / `GetAttribute()` | Attribute introspection |

### `Reflect::Function`

Represents a reflected method:

| Member | Description |
|--------|-------------|
| `GetName()` | Function name |
| `Invoke(void* obj, std::vector<void*> args)` | Invoke the method on an instance with raw pointer arguments |
| `HasFlag(const std::string&)` | Check for a function-level flag |

### `Reflect::Registry`

The singleton global registry of all reflected types:

| Method | Description |
|--------|-------------|
| `GetRegistry()` | Returns the singleton instance (lazy-initialized) |
| `GetClass(const std::string& name)` | Lookup by semantic name |
| `GetClass(uint64_t typeID)` | Lookup by type ID |
| `GetClasses()` | Returns all registered classes |
| `GetGroup(const std::string&)` | Returns all classes in a named group |

### `Reflect::Reflected`

Base interface for reflected types:

```cpp
class Reflected {
public:
    virtual const Reflect::Class& GetClass() = 0;
    virtual const std::string& GetTypeName() = 0;
    virtual uint64_t GetTypeID() = 0;
};
```

Classes using the `REFLECTED_BODY` macro implicitly implement this interface, enabling polymorphic type queries on reflected objects.

### Template Utilities

Two free template functions handle instance lifecycle:

```cpp
template <class T> T* CreateInstance(void* location = nullptr);
template <class T> void DestroyInstance(void* location, bool freeMemory);
```

These are stored as function pointers in `Reflect::Class` and called through `CreateInstance()` / `DestroyInstance()`.

---

## ObjectDescription Serialization

The **ObjectDescription** system (`ObjectDescription/`) provides a generic serialization/deserialization framework built on top of the reflection runtime.

### Core Concept

`Core::ObjectDescription` is a variant-like container that can hold:

| Type | Storage |
|------|---------|
| `String` | `Core::String` |
| `Uint` | `uint64` |
| `Sint` | `int64` |
| `Float` | `double` |
| `Bool` | `bool` |
| `Array` | `Core::Vector<ObjectDescription>` |
| `Object` | `std::unordered_map<Core::String, ObjectDescription>` |

### Type Converters

The system uses a template-based converter registry. Built-in converters exist for:

- **Primitives**: `uint8`, `uint16`, `uint32`, `uint64`, `int8`, `int16`, `int32`, `int64`, `float`, `double`, `bool`
- **Strings**: `Core::String`
- **Vectors**: `Math::Vector2`, `Math::Vector3`, `Math::Vector4`
- **Containers**: `Core::Vector<T>` (recursive)

Custom types can register converters via the `ADD_OBJECT_CONVERTER(Type)` macro, which stores a `ConverterBase*` in a global map keyed by `typeid(Type).hash_code()`.

### Reflection-Driven Serialization

When serializing an object, `ObjectDescription::SerializeObject()` checks two paths:

1. **Custom converter**: If a `ConverterBase` is registered for the type ID, use it directly (handles primitives, vectors, etc.)
2. **Reflection fallback**: If no custom converter exists, look up the type in `Reflect::Registry`, iterate all reflected properties, compute the memory offset (`obj + property.GetOffset()`), and recursively serialize each property by value.

Deserialization follows the mirror process: check for a custom converter first, then use reflection to walk properties and write values back into the object's memory layout.

### I/O Formats

ObjectDescription supports two serialization formats:

| Format | Header | Backend |
|--------|--------|---------|
| **YAML** | `YAMLObjectDescription.h` | yaml-cpp (`YAML::Node` / `YAML::Emitter`) |
| **JSON** | `JsonObjectDescription.h` | (json parsing library) |

Key functions:

```cpp
// YAML
ObjectDescription LoadObjectDescriptionFromYAMLFile(const fs::path& file);
void WriteObjectDescriptionToYAML(const ObjectDescription& desc, YAML::Emitter& out);

// JSON
ObjectDescription LoadObjectDescriptionFromJSONFile(const fs::path& file);
void WriteObjectDescriptionToJSON(const ObjectDescription& desc, ...);
```

---

## Build Pipeline Integration

The reflection pipeline is part of Nuke's C/C++ driver (`plugins/cpp/nuke/driver/reflection.py`, Nuke/README.md
section 9c). A project opts in with:

```toml
[project.driver]
reflection = true
```

which adds `Reflection`, `HeaderTool` and `CodeGen` to its `depends`, so they build first and the Reflection
library's include folder and `Reflection.lib` reach it.

### Per-source pipeline

For each `.c`/`.cpp` file of a reflecting project:

1. **Compile**: clang `-cc1` compiles the source to `<int>/<name>.obj`, with its dependency file.
2. **HeaderTool** (after a successful compile): the HeaderTool runs on the source:
   ```
   HeaderTool.exe --generated-folder <int>/generated --source-folder <project>/src source.cpp -- clang.exe -I... -D... -DHEADER_TOOL
   ```
   It parses the translation unit's AST, extracts reflection annotations from headers within `src/`, and writes
   `.reflected` YAML files to `<int>/generated/`. A failure fails the build.
3. **Incremental tracking**: a source compiles again when it, a file it read, its arguments or clang changed; it goes
   through the HeaderTool again when it compiled or the HeaderTool is newer than its object.

### Project-level code generation

After the sources compile, CodeGen (`CodeGen.exe --generated-folder <int>/generated --source-folder <project>/src`)
gathers the `.reflected` files into `<int>/generated/generated.cpp`, which is compiled and linked with the project.
CodeGen runs when a source compiled, when CodeGen is newer than the project's output, or when `generated.cpp` is newer
than its object (an interrupted build left it uncompiled).

### Without reflection

A project without `reflection = true` (the default) skips the HeaderTool and CodeGen: the reflection tools
themselves, third-party code, and projects that need no runtime type introspection.

---

## Usage Guide

### Marking a Class for Reflection

Use the `CLASS(...)` macro to annotate a class declaration. The macro expands to `__attribute__((annotate("reflect-class,...")))` when compiled with Clang, and to nothing when `STRIP_CLANG` is defined (for IDE compatibility):

```cpp
#pragma once
#include "Reflection.h"

namespace Engine {

class CLASS(GROUP=Components) TransformComponent
{
public:
    REFLECTED_BODY(Engine::TransformComponent);

public:
    PROPERTY() Math::Vector3 m_Position;
    PROPERTY() Math::Vector3 m_Rotation;
    PROPERTY() Math::Vector3 m_Scale;

    FUNCTION() void Update(float deltaTime);
    FUNCTION() void Reset();
};

} // namespace Engine
```

### Macro Reference

| Macro | Purpose | Expanded To (Clang) |
|-------|---------|---------------------|
| `CLASS(...)` | Annotate a class for reflection | `class __attribute__((annotate("reflect-class,...")))` |
| `PROPERTY(...)` | Annotate a member variable | `__attribute__((annotate("reflect-property,...")))` |
| `FUNCTION(...)` | Annotate a method | `__attribute__((annotate("reflect-function,...")))` |
| `REFLECTED_BODY(TypeName)` | Implement `Reflect::Reflected` interface + static class lookup inside the class body | Inline methods returning `Reflect::Class`, type name, and type ID |
| `LINK_REFLECTION_DATA` | Prevent linker dead-code elimination of generated registration code | Forward declaration + address-of reference to `DeadLink<project>()` |

### Required Setup Per Project

1. **Include `Reflection.h`** in headers that declare reflected types.
2. **Place `REFLECTED_BODY(Full::Type::Name)`** inside the class body (typically in the `public:` section). The argument must be the fully-qualified type name matching what Clang reports as `getQualifiedNameAsString()`.
3. **Place `LINK_REFLECTION_DATA;`** in at least one `.cpp` file that also `#include "Reflection.h"`. This ensures the generated registration code is linked into the final binary.
4. **Define `PROJECT_NAME`** in compiler defines (handled automatically by the build system based on the project folder name).

### Querying Reflection at Runtime

```cpp
#include "Reflection.h"

// Lookup a class by name
const Reflect::Class* cls = Reflect::Registry::GetRegistry()->GetClass("Engine::TransformComponent");

// Iterate properties
for (const auto& prop : cls->GetProperties()) {
    std::cout << prop.GetName() << " (offset: " << prop.GetOffset()
              << ", size: " << prop.GetSize() << ")" << std::endl;
}

// Create an instance through reflection
void* instance = cls->CreateInstance();

// Destroy it
cls->DestroyInstance(instance, true);

// Query groups (e.g., all components)
const auto& components = Reflect::Registry::GetRegistry()->GetGroup("Components");
for (const Reflect::Class* c : components) {
    std::cout << "Component: " << c->GetName() << std::endl;
}
```

### Serializing a Reflected Object

```cpp
#include "ObjectDescription/ObjectDescription.h"
#include "ObjectDescription/YAMLObjectDescription.h"

// Serialize
TransformComponent comp;
comp.m_Position = { 1.0f, 2.0f, 3.0f };

Core::ObjectDescription desc = Core::ObjectDescription::SerializeObject(&comp, comp.GetTypeID());

// Write to YAML file
YAML::Emitter out;
Core::WriteObjectDescriptionToYAML(desc, out);
std::ofstream fout("component.yaml");
fout << out.c_str();

// Deserialize from YAML file
Core::ObjectDescription loaded = Core::LoadObjectDescriptionFromYAMLFile("component.yaml");
TransformComponent comp2;
Core::ObjectDescription::DeserializeObject(&comp2, comp.GetTypeID(), loaded);
```

### Attribute Conventions

Common attribute patterns used in the codebase:

| Attribute | Type | Purpose |
|-----------|------|---------|
| `GROUP=<name>` | Property | Assigns a class to a named group for registry queries (e.g., `GROUP=Components`) |
| Custom flags | Flag | Boolean markers for editor/tool behavior (e.g., `Hidden`, `ReadOnly`) |

Attributes are parsed from the annotation string after the `reflect-class,` / `reflect-property,` / `reflect-function,` prefix. They are available at both compile time (in generated code) and runtime (through `GetAttributes()` / `HasAttribute()`).

---

## File Reference

| File | Purpose |
|------|---------|
| `Reflection/src/Reflection.h` | Runtime API: `Reflect::Class`, `Property`, `Function`, `Registry`, macros |
| `Reflection/src/Reflection.cpp` | Registry singleton implementation, `AddClass()` logic |
| `HeaderTool/src/Source.cpp` | HeaderTool entry point — Clang tool invocation |
| `HeaderTool/src/ReflectionConsumer.h` | AST consumer declaration |
| `HeaderTool/src/ReflectionConsumer.cpp` | AST visitor: class/property/function extraction, `.reflected` YAML emission |
| `StaticReflection/src/StaticReflection/ReflectionData.h` | Shared data model: `Class`, `Property`, `Function`, `File`, `ReflectionData` |
| `StaticReflection/src/StaticReflection/ReflectionData.cpp` | Serialization/deserialization of static reflection data, attribute parsing |
| `StaticReflection/src/StaticReflection/Utils.h` | Tokenization helpers for attribute string parsing |
| `CodeGen/src/Source.cpp` | CodeGen entry point — loads `.reflected` files, invokes `WriteCode()` |
| `CodeGen/src/CodeGen.h` / `.cpp` | C++ code generation: function wrappers, offset helpers, registry registration |
| `CodeGen/src/Validate.cpp` | Stale `.reflected` file cleanup |
| `ObjectDescription/src/ObjectDescription/ObjectDescription.h` | Variant container, converter templates, type system |
| `ObjectDescription/src/ObjectDescription/ObjectDescription.cpp` | Reflection-driven `SerializeObject()` / `DeserializeObject()`, converter registrations |
| `ObjectDescription/src/ObjectDescription/YAMLObjectDescription.h` / `.cpp` | YAML I/O via yaml-cpp |
| `ObjectDescription/src/ObjectDescription/JsonObjectDescription.h` / `.cpp` | JSON I/O |
| `Builder/ObjectBuilder.py` | Build pipeline: per-source-file compilation + HeaderTool invocation |
| `Builder/ProjectBuilder.py` | Build pipeline: project-level CodeGen and `generated/generated.cpp` compilation |
