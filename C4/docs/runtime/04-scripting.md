# Scripting

A C4 program can compile C4 source files while it runs and call into them: scripts. A script is ordinary
C4 (`.cf` / `.ch` files), compiled in the running program by the embedded compiler and linked into it with
LLVM ORCv2. Script code shares the program's garbage collector, type system, reflection registry, threads and
exceptions: a script derives from the program's classes, implements its interfaces, instantiates its
templates, throws to it and catches from it, and the program finds the script's types and functions through
reflection. A script can be unloaded and compiled again (hot reload). A script that crashes does not take the
program down (section 7).

The program that runs scripts is the **host**. A host is built with `--script-host`, which also writes a compact
**interface** of the program beside the executable (`Game.exe` -> `Game.c4i`). A script compiles against that
interface alone: the host's sources, the C4 install and the toolchain are not needed where the scripts run.

Design decision: D-49 ([plan](../status/plan.md#3-design-decisions)). Work package: B30.

Sources:
- Host build: `C4Lib/src/Script/` (`HostTable`: the symbol table compiled into the host and the thread_local
  accessors, `InterfaceText`: a file's text without its non-template bodies, `InterfaceBuilder` and
  `HostInterface`: the `.c4i` and its format, `InlineBodies`: the bitcode of the host functions scripts may
  inline), driven by `--script-host` in `src/Source.cpp`.
- Script compile, link, load and guarded calls: `C4Lib/src/Script/` (`ScriptSources`: the interface and the
  script's files registered for one compile, `ScriptCompiler`: compile and script-mode lowering,
  `ScriptModulePasses`: the host-wins and runtime passes over the module, `ScriptJit`: linking and memory,
  `ScriptHost`: load, calls, unload, reload), built into `C4Script/` (`C4Script.dll`, its C API in
  `C4Script/src/C4Script.h`, the guard in `C4Script/src/GuardedCall.cpp`).
- Runtime support: `C4Runtime/ScriptRuntimeC.h` with `C4Runtime/Script/` (`ScriptExitsC.c`: per-script exit
  lists, `ScriptTlsC.c`: emulated thread storage, `ScriptThrowC.c`: script images and their throws), the GC's
  root tables and finalizer dropping (`C4Runtime/GC/GCApi.c`), and stacked reflection registrations
  (`C4Runtime/Reflect/ReflectShadows.c`).
- C4 API: `Bedrock/Scripting/` (namespace `Scripting`, opt-in through `include "scripting.ch"`).

---

## 1. Using scripts

```c4
include "scripting.ch"

int32 main()
{
	Scripting::Host* host = Scripting::Host::Open();                  // reads Game.c4i beside Game.exe
	if (!host->IsOpen()) { printf("%s\n", host->Error()); return 1; }
	Scripting::ScriptOptions options;
	options.name = Core::String("enemy");                              // also its reflection tag
	options.AddFile("scripts/enemy.cf");
	Scripting::Script* script = host->Compile(options);
	if (!script->Ok()) { printf("%s", script->Diagnostics()); return 1; }

	Reflection::Function spawn = script->FindFunction("Enemy::Spawn");
	int32 count = 3;
	if (!script->Invoke(spawn, nullptr, count)) printf("%s\n", script->FaultText());   // a crash in the script

	script = host->Reload(script);                                     // compiles its files again and swaps it in
	script->Unload();
	return 0;
}
```

```c4
// scripts/enemy.cf - an ordinary C4 file
include "engine/entity.ch"              // a header of the host, from the interface

namespace Enemy
{
	class Grunt : Entity                 // a host class
	{
		[operator("create")] public void Init() { health = 10; }
		public override void Update(float dt) { position.x += dt; }
	}

	void Spawn(int32 count)
	{
		for (int32 i = 0; i < count; i++) World::Add(new Grunt());    // host function, host template inside
	}
}
```

| Rule | Status |
|---|---|
| A host is built with `--script-host`; `Scripting::Host` in a program built without it is a compile error that says so. | **Done** (ToolsTest `ScriptHostFlag`) |
| The build writes `<out>.c4i` beside the output and copies `C4Script.dll` from the install beside it. Both ship with the program. | **Done** (every ScriptTest host) |
| A script is one or more files compiled together as one unit; its files may include each other and any host header the include allow-list permits. | **Done** (ScriptTest `MultiFile`, `Diagnostics`) |
| Scripts compile on any thread, several at once; loading and unloading are serialized by the host. | **Done** (ScriptTest `Concurrency`) |

---

## 2. What crosses the barrier

Everything in the language works in a script as in the host. Across the boundary:

| Feature | Rule | Status |
|---|---|---|
| Host functions and methods | Called directly (the host's code, through the symbol table); small ones are inlined into the script (section 4.4). | **Done** (ScriptTest `HostCalls`) |
| Host globals and static members | The host's storage: a script reads and writes the same variables. | **Done** (ScriptTest `HostCalls`) |
| Host classes | A script creates them, derives from them (vtables, RTTI and casts work both ways) and implements host interfaces; the host calls script overrides through its own virtual calls. | **Done** (ScriptTest `Classes`) |
| Host templates | A script instantiates them with its own types or with host types. An instance the host also made is the host's (its code and static data members); one the host never made is the script's own copy (section 4.3). | **Done** (ScriptTest `Templates`) |
| Script templates | Ordinary templates of the script. | **Done** (ScriptTest `Templates`) |
| Lambdas, `func<R(A)>`, function pointers | Pass both ways; a pointer to a script function is valid until the script unloads. | **Done** (ScriptTest `Callbacks`) |
| Exceptions | Thrown and caught across the boundary in both directions, by type, with the host's and the script's types alike. | **Done** (ScriptTest `Exceptions`) |
| GC | One heap: script allocations, script globals and thread_locals are roots and are scanned like the host's; finalizers of script types run as usual. | **Done** (ScriptTest `GC`) |
| Reflection | The script registers its own types, enums, globals and functions under its tag (the script's name); the host finds them like its own; host entities are not registered again. | **Done** (ScriptTest `Reflection`) |
| Globals and static locals of a script | Started when the script loads (in declaration order, as in a program), destroyed in reverse order when it unloads. | **Done** (ScriptTest `Lifecycle`) |
| `thread_local` in a script | Per thread; one whose start is not a constant starts on its first use on each thread; destroyed when the thread ends or the script unloads. | **Done** (ScriptTest `ThreadLocals`) |
| Host `thread_local` from a script | The calling thread's object of the host. | **Done** (ScriptTest `ThreadLocals`) |
| Macros | A host header's macros reach the script files that include it, as between host files. | **Done** (ScriptTest `Macros`) |
| The processor (D-62) | A script compiles and runs for its host's target (the `.c4i` records its CPU and features): it sees the host's `SIMD_*` macros, may use what the host may (`simd_fma` in an FMA host), and passes vectors in the same registers, 256- and 512-bit ones included. | **Done** (ScriptTest `WideVectors`, `WideVectorsAvx2`) |
| Projects (D-59) | A script is its own project, depending on the host projects the allow-list names. | **Done** (ScriptTest `MultiFile`, `Diagnostics`) |
| `[test]` functions in a script | Ordinary functions; `Script::RunTests` runs them with the test harness. | **Done** (ScriptTest `ScriptTests`) |
| C/C++ interop in a script | A script uses every binding the host imported (they are part of the interface). A script cannot import a new header: that needs CBind and clang, which a shipped program does not have. | **Done** (ScriptTest `Interop`) |
| Debugging a script | Not yet: JIT code has no PDB. | **Planned** (later) |

---

## 3. The host build

`C4.exe --script-host ... -o Game.exe` does three things beyond a normal build.

### 3.1 The symbol table

The host image exports nothing, and its runtime is linked statically, so the script linker cannot find host
code by name through the OS. Instead the build compiles a table into the image:

```
__c4_script_table = { build id (16 bytes), count, address[count] }
```

`address[i]` is the address of the host symbol whose name is entry `i` of the interface's symbol list (section
3.2). The list (`Script/HostTable`) holds every function the host emitted with a body (template instances,
compiler-written functions and lambda bodies included), every global, static member and static local with storage
of its own (with their once guards; not the private constants such as strings and RTTI tables, which a script
makes for itself), every declaration the host's code uses (runtime and C functions), the C functions the prelude
declares, every function and global the host's C and C++ objects define (its
`extern "c" source` files and binding shims, read from the objects, so a script reaches every binding, not only the
ones the host calls), every runtime entry point script code may call (`ScriptRuntimeEntryPoints`), and an accessor
per host `thread_local` (`__c4_tls_<sym>`, which returns the calling thread's address). A host build gives every
class with virtual methods its vtables (`__c4_vtable_<type>_<offset>`) and lists them, so a script's objects of
the host's classes use the host's vtables and outlive the script. Type descriptors (`??_R0...`) are not listed:
RTTI and exception tables name them image-relatively, so a script makes its own, which match the host's by name. Referencing them from the table
also keeps the linker and the optimizer from removing any of them, so a script can call a host function nothing in
the host calls. A C function the table does not list is found among the exports of the DLLs the process loaded.

The build id is a hash of the whole interface; the image's table carries the same id, and a script host refuses
an interface whose id differs from its image's.

### 3.2 The interface (`.c4i`)

One binary file:

| Section | Content |
|---|---|
| Header | magic `C4IF`, format version (2), build id, compiler version |
| Target | the host's LLVM CPU name and feature string (D-62); a target the reading compiler does not know is refused |
| Projects | each project's name and dependencies (D-59), so includes resolve as in the host |
| Folders | how many source folders the host registered; each project names its own |
| Files | every source of the host in registration order (its sources, the Bedrock files it uses, the prelude): its unit id, project, folder and path in it (or its path in the library), whether it is a library file or the prelude, its **interface text** and the macros it exports |
| Symbols | the names of the table's entries, in table order |
| Reflected types | the qualified names of the types the host registered, so a script does not register them again |
| Inline bodies | LLVM bitcode of the host functions a script may inline (section 4.4) |

A file's **interface text** is its text after the preprocessor and the interop pass (macros expanded,
conditional blocks resolved, bindings appended), with the body of every function that is not part of a template
removed: a definition becomes a declaration, an out-of-line method definition disappears (its declaration in the
class stays). Template bodies stay, as does everything else: types, fields and their initializers, enums,
global declarations with their initializers (constant folding needs them), aliases, annotations, default
arguments, comments outside bodies (a header's documentation). Lines stay in place, so a diagnostic in a host
header names the line it names in the host's source.

A small Bedrock program's interface is about half a megabyte: 130 KB of text for 62 files, 340 KB of inline
bodies, 1300 symbols. The vendored LLVM is built without zlib or zstd, so the file is not compressed.

Nothing in C4 evaluates a function body at compile time (there is no `constexpr` and no deduced return type), so
a declaration is all a script needs of a non-template function.

### 3.3 Inline bodies

To keep script code as fast as host code, small host functions are inlined into scripts across the boundary.
The build writes the lowered (not yet optimized) IR of every host function that is small enough (at most
`kInlineBodyLimit`, 160, instructions) and refers only to what a script can name (table symbols, intrinsics and
private constants, which are copied with it) and no `thread_local`, as one bitcode module (`Script/InlineBodies`). A script imports a body as `available_externally` when it calls the function: the optimizer may
inline it, and every call it does not inline goes to the host's code. Template instances the host made need no
bitcode: the script lowers their bodies from the template (section 4.3).

### 3.4 What the build ships

`Game.exe`, `Game.c4i` and `C4Script.dll` (the compiler, LLVM and the JIT; about the size of `C4.exe`). The
scripts' own files are the program's data.

---

## 4. Compiling a script

`Host::Compile` runs in `C4Script.dll`, on the calling thread, with a fresh compiler instance:

1. **Register.** The interface's files first, in their recorded order and under their recorded unit ids (so every
   name that depends on registration order comes out as it did in the host), each marked as an interface unit;
   then the script's files, as the script's project. A script file may come from disk or from memory.
2. **Preprocess.** Interface files are not preprocessed again (their text is already expanded); each provides the
   macros it exports. Script files are preprocessed as in a build, inheriting the macros of the headers they
   include. A script file with an `extern "c"` / `extern "c++"` directive is an error (section 2).
3. **Parse and build the model**, as for a program. The allow-list (section 4.5) is checked as includes resolve.
4. **Lower in script mode** (section 4.2), with no entry point, for the host's target (its macros in step 2 too).
5. **Import inline bodies** (section 3.3), then optimize at the requested level (`-O0` or `-O2`, the same pipeline
   as the host's build).
6. **Link** into the script's own JIT library (section 6), resolving every undefined name through the host's
   table, then the C functions of the DLLs the process has loaded (for `[link]` declarations only).
7. **Load**: register the script's reflection, its GC roots and its exit lists, then start its globals, inside a
   guard (section 7).

Errors at any step come back as diagnostics, in the same form the compiler prints; nothing is loaded.

### 4.1 Names that must agree

A script names a host entity by the symbol the host gave it, computed again from the interface, so every
symbol must follow from the program, not from how far the compiler had got: template instances are named for
their arguments (`Box<int32>`'s scope is `BoxTint32E`), a lambda for the function that writes it and its
position there, a compiler-written function for its type and operation. When the script asks for a host symbol
the table does not have, that is a link error naming it.

### 4.2 Script-mode lowering

| Entity | In script mode |
|---|---|
| A function from an interface file | Declared, not defined: the host's code. A virtual or lifecycle method the host defined counts as implemented. |
| A global, static member or static local from an interface file | An external declaration of the host's storage; not started, not destroyed, not a root, not registered. |
| A template instance (function, class member, static data member) | The host's if the table has its symbol: declared, with the script's own lowering of the body kept `available_externally` for inlining. Otherwise defined by the script. |
| Script functions and globals | Defined as in a program. |
| Start and exit of script globals | `__c4_script_init` (called by the loader) instead of a static constructor; exit-time destruction goes to the script's own exit list (`c4_script_at_exit`), run at unload. |
| Script `thread_local` | Emulated thread storage (`__emutls_get_address`, implemented by the runtime): a JIT image has no PE TLS directory. One whose start runs code gets `__c4_tls_start_<sym>`, a per-thread once around its start, which code calls (an invoke inside a `try`) before it takes the variable's address; the start's own accesses do not. |
| A host class's vtable | The host's: the script's copy becomes a declaration of it. |
| RTTI and exception tables | The script's own, the type descriptors of host types included. |
| Host `thread_local` | Through the host's accessor `__c4_tls_<sym>`. |
| `throw` | Calls the runtime's script throw, which gives the exception the script image's base (section 6.3). |
| Reflection | Registered by `__c4_script_reflect_init` under the script's tag; host entities and the host's template instances are left out. |
| GC roots | The script's own root table, registered at load. |

### 4.3 Template instances

An instance the host made is shared: the script calls its code and uses its static data members, so a counter
in `Box<int32>` counts for host and scripts together. An instance the host never made is the script's own,
with its own static data members, as with a template instantiated in two DLLs in C++. Two scripts that make the
same instance each have their own; reflection then names the one registered last, and unloading it brings the
other's back (section 5).

### 4.4 Optimization

The script is optimized as a whole program would be, with the host's small functions available for inlining
(section 3.3) and the host's template instances inlinable from their templates. `-O2` is the default;
`ScriptOptions.optimize = false` compiles at `-O0` (faster to compile, easier to read in a dump).

### 4.5 The include allow-list

`ScriptOptions::Allow` names the host projects (or interface path prefixes) a script may include. An include
outside them is a compile error: `a script may not include 'x': it is not on the allow-list`. No entry allows
every host header. Bedrock is always allowed.

---

## 5. Reload and unload

`Host::Reload(script)` compiles the script's options again; when the new script compiles and links, the old one
unloads and the new one loads (nothing loads in between), and when it does not, the old one stays loaded and the
returned script carries the diagnostics. `Script::Unload` (and the unload a reload does) runs, in order: the reflection unload listeners of the script's tag, the script's exit list (global
and static-local destruction, last started first), its thread storage on every thread (destroyed and freed), the
removal of its GC roots, its reflection entries (`c4_reflect_unload_source(tag)`), and finally its code and data.

Rules:
- Nothing may still run the script's code while it unloads, and the host must drop every pointer into it (script
  objects, function pointers, reflection handles of its entities) first. Objects of script types that are still
  alive keep their memory, but their finalizers are dropped (they would call freed code): unload reports how
  many there were.
- Reflection keeps every registration of an identity: registering one that another tag holds puts the new one in
  front; unloading a tag removes only its own, so the previous one is found again.
- State that must survive a reload is the host's to carry (for example through `Serialization` and reflection,
  before unloading and after loading).

---

## 6. Linking and the JIT

### 6.1 Libraries

One ORC `LLJIT` per script (its own session, so scripts compile and link on any thread at once), for the host's
CPU and features rather than the running machine's, so host and script agree on how vectors are passed. The script's
undefined names resolve through the host's table (by the interface's symbol names), then through the exports of
the DLLs loaded in the process (for C functions a `[link]` declaration names). Unloading a script destroys its JIT
and frees its memory.

### 6.2 Memory and unwind tables

Each object is loaded into one block (code first, within 4 GiB of its data, as the image-relative tables need).
Its `.pdata` is registered with `RtlAddFunctionTable`, relative to the block's lowest section (the base
RuntimeDyld resolves image-relative relocations against), and deregistered when the script unloads. Function
tables of different objects never overlap.

### 6.3 Exceptions

MSVC exception tables are image-relative. The script image gets:
- a personality routine of its own (`__CxxFrameHandler3` in the image, jumping to the host's), because `.xdata`
  names the personality by an image-relative offset;
- a throw (`c4_script_throw`) that raises the C++ exception with the script image's base as the base of its throw
  information (what `_CxxThrowException` takes from `RtlPcToFileHeader`, which knows no JIT image).

A host function that throws into a script must be able to unwind: a C or C++ function compiled as `extern "C"`
with `-fexternc-nounwind` (Frost's default) has no unwind table, and an exception crossing it skips the frames
above.

---

## 7. Crash isolation

Every call the host makes into a script through the scripting API (`Script::Call`, the loader's starts and
exits, `Scripting::Guard`) runs inside a structured-exception guard. A hardware fault in the script (access
violation, illegal instruction, divide by zero, stack overflow) stops that call: the call returns false, the
script is marked faulted, and `Script::FaultText()` says what happened and where (`exception 0xc0000005 at
script+0x1a2, writing 0x0`). A faulted script stays loaded
until the host unloads it, so the host decides; calling into it again is refused.

A C4 exception that escapes a guarded call is not a fault: it propagates to the caller like any exception. A
global whose start faults (or throws) fails the load: the diagnostics say so, and nothing of the script stays
loaded.

The guard lives in `C4Script.dll`, not in the runtime: the runtime is compiled with `-mstackrealign`, under
which clang (LLVM 22) miscompiles SEH `__try` / `__except` (at `-O2` the handler crashes, at `-O0` the filter
writes its findings to the wrong frame).

What the guard cannot cover: a script's code called by host code that is not guarded (a virtual call into a
script object from the engine's own loop). The host wraps such calls in `Scripting::Guard` where a script may run.
There are no memory quotas.

---

## 8. Status

| Item | Status |
|---|---|
| Feasibility: JIT C4 code with exceptions in every direction (a spike, now ScriptTest `Exceptions`) | **Done** |
| Deterministic symbols (section 4.1; C4Testing `Semantic.StableSymbols`) | **Done** |
| Reflection registration emitted as IR (no generated C file; needed by the JIT, also used by builds; C4Testing `Semantic.ReflectionImage`) | **Done** |
| `--script-host`: symbol table, thread_local accessors, interface, inline bodies, `C4Script.dll` beside the output (C4Testing `Script.InterfaceText`, `Script.HostInterface`, `Script.HostTable`, `Script.InterfaceBuilder`; ToolsTest `ScriptHostFlag`) | **Done** |
| `C4Script.dll`: compile, script-mode lowering, link, load, unload, reload (C4Testing `Script.Compiler`; ScriptTest, every case at `-O0` and `-O2`) | **Done** |
| Runtime: script exit lists, emulated thread storage, script images and throws, root tables, finalizer drop, stacked reflection registrations (IsolatedTest `Scripting/ScriptExits`, `Scripting/EmulatedThreadStorage`, `MemoryGC/RootTablesAndDroppedFinalizers`, `Reflection/StackedRegistrations`) | **Done** |
| Guarded calls (in `C4Script.dll`; ScriptTest `CrashIsolation`) | **Done** |
| `Bedrock/Scripting` (ScriptTest) | **Done** |
| Debugging scripts | **Planned** (later) |
