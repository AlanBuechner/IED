# 13 - C and C++ interop

C4 is meant to drop into existing C/C++ code and SDKs (Win32, D3D12, engine code) without
hand-written bindings. A C4 file names a C or C++ header or source file with an `extern`
declaration; the compiler generates C4 declarations from headers with CBind (a libclang tool),
compiles foreign sources with the bundled clang, and links everything into one executable. The
C4 ABI matches clang's Windows x64 ABI so calls cross the boundary directly, with no marshaling.

See also: [12 - Annotations](12-annotations.md) (`[link]`, `[uuid]`),
[compiler/04-codegen-and-abi.md](../compiler/04-codegen-and-abi.md),
[tools/01-compiler-cli.md](../tools/01-compiler-cli.md).

---

## 1. extern declarations

**Status:** **Implemented**

```c4
extern "c" include "native.h"            // generate C4 declarations for a C header
extern "c" source "native.c"             // compile and link a C file
extern "c++" include "cpp.h"             // C++ header (classes, methods, namespaces)
extern "c++" source "cpp.cpp"            // compile and link a C++ file

extern "c++" include "game.hpp" {        // optional configuration block
	include_dir("C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/um");
	include_dir("C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/shared");
	define("WIN32_LEAN_AND_MEAN");
	lib("d3d12.lib"); lib("dxgi.lib"); lib("user32.lib");
}
```

| Part | Meaning |
|---|---|
| `"c"` / `"c++"` | language of the foreign file (selects `-std=c11` or `-std=c++20` and C vs C++ binding rules) |
| `include "path"` | bind a header: its declarations become visible in this file |
| `source "path"` | compile the file with the bundled clang and link the object |
| `define("NAME")`, `define("NAME", "VALUE")` | preprocessor define for the foreign parse/compile |
| `include_dir("dir")` | header search directory for the foreign parse/compile |
| `lib("x.lib")` | link an import/static library; bare names are resolved against the newest installed Windows SDK. `-l x` / `-L dir` on the command line do the same for a whole build |

Rules:

- `extern` declarations appear only at file scope (like `include`), and the trailing `;` is
  optional.
- Paths are relative to the declaring file, or absolute. A foreign header is only ever bound, never read as C4,
  wherever it lies (an absolute path into a toolchain's `C4Api/` included).
- A foreign source also sees the runtime's C headers, so native code can register with it: `#include
  "ReflectABI.h"` gives the reflection registry (`c4_reflect_register_type`, ..., see
  [runtime/02-reflection.md](../runtime/02-reflection.md)).
- The interop pass runs as a text pre-pass after macro expansion, so the generated declarations
  behave exactly like hand-written C4 declarations in the file: they follow the normal
  include-visibility rules of the [program model](01-program-model.md). They are added after the
  file's own text (file-scope declarations are order independent), so a diagnostic in the file keeps
  its line number.
- Bindings may share declarations, as C translation units share headers: two projects binding `windows.h` (one
  through its own `Win32.h`, one through `d3d12.h`), or one file binding two headers that include a third. A
  binding's declaration that another binding has made already, with the same text, is that one: it is not defined
  again, and every file whose binding repeats it sees it as its own and its project reaches it
  ([15-projects.md](15-projects.md) rule 4). The same name declared differently is still a redefinition. The
  wrappers a C++ header's shim makes are `inline`, so the linker keeps one of each when several shims make them
  (BUG-420).
- Every interop failure is a hard build error: unresolvable path, CBind missing, CBind rejecting
  the header, or the foreign source failing to compile.

---

## 2. What bindings look like

CBind maps foreign declarations to C4 declarations:

| C / C++ | C4 binding |
|---|---|
| free function | declaration-only C4 function with `[link("symbol")]`; a C-linkage function is reached at its own symbol, body or not |
| `static` or `inline` function the bound header defines | reached through a wrapper CBind writes into a shim (`cbind_fn_<name>`, a C shim for a C header), so its body is compiled; a system header's own inline helpers are not bound |
| object-like `#define` that is a constant | a C4 constant of C's type: `#define WM_CLOSE 0x0010` is `const int32 WM_CLOSE = 16;`, unsigned macros `uint32` / `uint64`, 64-bit ones `int64`, floats `float` / `double`, narrow strings `const char*`. Each is evaluated by clang, so `(A | B)` and casts fold; a macro that is not such a constant (a type, a statement, `{0}`), a function-like one, or one named like a bound declaration or starting with `_` is left out. Two bindings in one file declare a constant once |
| struct / class layout | C4 `struct` with clang's layout, byte for byte (B57); see below for what has no C4 spelling |
| union | C4 `union` of every member; an anonymous struct inside it is a struct view |
| anonymous union in a struct | a C4 anonymous union of every arm (`overlapped.Pointer`, `barrier.Transition.pResource`), struct arms as struct views |
| anonymous struct in a struct, or (Microsoft) a named struct embedded without a field name | its members are the owner's |
| field of an anonymous record type | that type, named after its owner and field: `in_addr.S_un` is an `in_addr_S_un` |
| struct defined inside another | bound under its own name |
| bitfields | the integers that store them, named after the run: `uint32 __bits_a_b_c` (C4 has no bitfields: read with shifts and masks) |
| a member C4 cannot spell (`__int128`) or place (a member of a `#pragma pack` record that is not aligned) | its bytes, under its name: `uint8[16] big` |
| flexible array member | left out (it has no size); the record keeps C's size |
| a struct only declared, never defined (an opaque handle: `typedef struct c4a_parse c4a_parse;`) | `void*` wherever a pointer to it appears; the name itself is not bound |
| C++ namespace | flattened: `mathlib::Dot` is called as `Dot` |
| constructor | `[operator("create")]` extension method, so `new Counter(40)` works |
| instance method | extension method on the type: `c->Add(5)` |
| overloaded methods | the C++ name; C4 overload resolution picks one: `b->Deposit(100)`, `b->Deposit(50, 5)` |
| `const` method | a const extension method, callable on a `const T&` |
| static method | free function `Type_Method`: `Counter_Scale(6, 7)` |
| static data member | `[link("<decorated name>")] public static T name;` in the type: `Counter::g_global` reads and writes the C++ variable |
| `private` / `protected` field | the same access in C4 |
| `int`, `unsigned int`, `long`, `unsigned long` | `int32`, `uint32` (`long` is 32-bit on Windows) |
| `char`, `signed char`, `unsigned char` | `char` (`char8`), `int8`, `uint8` |
| `wchar_t`, `char16_t`, `char32_t` | `char16`, `char16`, `char32`: `u"text"` passes to a wide API |
| `const T*`, `T&`, `const T&` | `const T*`, `T*`, `const T*` (a reference is passed as its address) |
| virtual method | extension method that calls through a generated vtable trampoline shim (compiled and linked automatically), so virtual dispatch through a base pointer works |
| `__declspec(uuid(...))` / `MIDL_INTERFACE` | `[uuid("...")]` on the type |
| `std::initializer_list<T>` parameter | `__init_list<T>` - pass a braced list: `SumInts({1, 2, 3})` |
| function-pointer parameter | `funcptr<R(A...)>` - pass a C4 function: `CatchFromCallback(MyCallback, 4)` |
| destructor | an extension method annotated `[operator("delete")]`: it runs at scope exit, on `delete`, for a member of a C4 type, during unwinding and when the collector reclaims the object |

Example (from `assets/IsolatedTest/Interop/CPlusPlusInterop`):

```c4
extern "c++" include "cpp.h"
extern "c++" source "cpp.cpp"

void TestCounter() {
	Counter* c = new Counter;
	c->Add(5);
	Counter* c2 = new Counter(40);
	int32 s = Counter_Scale(6, 7);
	delete c;
	delete c2;
}
```

### 2.1 Caching

Generated bindings are cached as `<hash>_<header>.c4bind` files (the hash is of the header's
absolute path). A cached binding is reused while the header, CBind.exe, the language, the defines,
the include dirs and the build's target flags are unchanged. The bindings, their stamps, the shims and the foreign
objects' records are read and written with LLVM's file calls (`Compiler/FileIo`), so a build or project folder deep
enough that their paths pass `MAX_PATH` works.

---

## 3. Binding single symbols with `[link]`

**Status:** **Implemented**

A function or variable declaration without a body/initializer can name a foreign symbol
directly. A function *definition* with `[link("sym")]` is emitted under that symbol instead, so foreign
code calls it by that name (`[link("on_event")] void OnEvent() { ... }`):

```c4
extern "c" source "ext.c"

[link("g_extern_counter")] int32 g_counter;   // reads and writes go to the C global

int32 main() {
	g_counter = 55;
	return 0;
}
```

This is what generated bindings use internally. A `[link]` variable has no initializer: the foreign
definition gives its value (`'g' is the foreign variable 'sym' ([link]): it has no initializer, ...`).

---

## 4. ABI compatibility

**Status:** **Implemented** (Windows x64)

C4 lowers calls exactly as clang does for `x86_64-pc-windows-msvc`, which is what makes direct
calls possible:

- By-value aggregates of 1, 2, 4 or 8 bytes pass and return in one integer register; other sizes
  pass by hidden pointer and return through `sret`.
- Methods of C4 types use the MSVC member convention (`this`, then the result pointer; every object
  result goes through it, even one of 8 bytes), and C4 objects have MSVC layout, vtables and RTTI. Calls of C++
  methods use it too, so the COM methods every D3D12 renderer calls for a struct by value
  (`GetCPUDescriptorHandleForHeapStart`, `GetDesc`, `GetResourceAllocationInfo`) return the right values (test
  `Interop/ComStructReturns`: virtual methods returning 8-, 16- and 24-byte structs, one with arguments after the
  hidden pointer, on an object C++ made). A C4 class that implements
  an interface can therefore be handed to C++ code that calls it through the matching C++ abstract class
  (test `Interop/CppExceptionsAndInterfaces`).
- Exceptions cross in both directions: type descriptors carry MSVC decorated names, so a C4
  `catch (int32 e)` catches a C++ `throw 7;`, the CBind binding of a C++ struct catches that struct, and
  C++ `catch (int)` / `catch (const T&)` / `catch (...)` catch C4 throws.
- References lower to pointers.
- C vector types (`__m128`, `__m128i`, `__m256`, `__m512`, any `vector_size` vector of 2 to 16 lanes) bind as the
  `packed T[N]` of the same lanes and pass as clang passes them for the build's target: XMM, YMM or ZMM, split when
  the target lacks the register (tests `Interop/SimdVectorArguments`, `Interop/SimdIntrinsicsInC`). A vector C pads
  (a 3-lane `ext_vector_type` takes 16 bytes) has no binding. Pure packed vector objects pass the same way.
- Foreign sources compile, and CBind parses headers, with the build's `-march` / `-m` flags (D-62), so C sees the
  same processor (`__AVX2__`) and passes vectors the same way. Foreign sources and shims also take the build's `-O`
  level and `-g` (B59), and an object is kept from one build to the next while its source, every header clang read
  for it, its flags and clang itself are unchanged.
- `void*` converts to and from any pointer (and `T**` to `void**`), which covers COM out-params
  such as `IID_PPV_ARGS` patterns.
- Integer-to-pointer casts are allowed for handle-style APIs.

Details: [compiler/04-codegen-and-abi.md](../compiler/04-codegen-and-abi.md).

## 5. COM

**Status:** **Implemented**

COM interfaces from real SDK headers (`d3d12.h`, `dxgi`) work: CBind emits `[uuid]` on each
interface, `__uuidof(T)` returns a pointer to a `_GUID`, interface methods are callable through
the vtable trampolines, and `void**` coercion handles out-params. The D3D12 tests
(`assets/IsolatedTest/Interop/Dx12*`, `assets/StressTest/Dx12Game`) need a DX12-capable GPU to run.

`_GUID` is declared by the prelude ([14](14-builtins.md)); a program or a bound header that declares its own
hides it. A type without `[uuid]` gets a name-based GUID, so `__uuidof(T)` works for every type.

### 5.1 Exporting data from the executable

**Status:** **Implemented** (through a foreign source)

Some loaders find data in the executable that starts them by its exported name. The D3D12 runtime looks for the
Agility SDK through two of them, `D3D12SDKVersion` and `D3D12SDKPath`: a program that exports them gets the SDK's
`D3D12Core.dll` (enhanced barriers, Shader Model 6.6 on a Windows 10 whose own runtime has neither) instead of the
system's. C4 cannot export data itself, but a foreign C source can, and the export survives C4's link, whole-program
and in partitions (`--incremental`), including an object the cache reused:

```c
// agility.c - in C a file-scope const has external linkage (`extern` with an initializer only warns)
__declspec(dllexport) const unsigned int D3D12SDKVersion = 614;
__declspec(dllexport) const char* D3D12SDKPath = ".\\D3D12\\";
```

```c4
extern "c" source "agility.c"
```

The SDK's `D3D12Core.dll` goes beside the executable in the folder the path names (`D3D12\`), copied there by a
build step. Tests: `Interop/ExportedData` (the program finds both with `GetProcAddress(GetModuleHandleA(NULL), ...)`
and reads them, at `-O0` and `-O2`) and `Interop/ExportedDataIncremental` (the same in 4 partitions with
`--incremental`). Whether C4 should export a constant itself (`[export("D3D12SDKVersion")]`) is a roadmap item.

---

## 6. Limits and planned work

| Item | Status |
|---|---|
| Windows x64 only (MSVC ABI, Windows SDK lookup) | **Planned**: Linux with the Linux port |
| No timeout or progress diagnostic when CBind binds a very heavy SDK header | **Planned** |
| C++ templates, operators and overloaded operators in headers | **Partial** - not covered by tests; assume unsupported |
| Calling C4 functions from C/C++ (exports with a stable name) | **Partial** - C4 symbols use C4 mangling (`Name_<n>`); callbacks work by passing function pointers (`Win32WindowCallback` test); a definition with `[link("sym")]` is emitted under `sym` |
| Exporting data from the executable | **Implemented** through a foreign C source (section 5.1); C4 itself exports no data (roadmap) |
| Tests hard-code Windows SDK version `10.0.26100.0` | test hygiene |
