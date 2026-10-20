# Building the compiler

The C4 compiler, its libraries and tools are C++ and are built with **Frost**, the project's C++ build
system. C4 programs themselves are built with Nuke (see [02-nuke.md](02-nuke.md)).

The C4 tree is the `C4/` folder of the IED repository; "the repository root" below is the folder above it.

---

## 1. Pieces

| Piece | What it is |
|---|---|
| `setup.bat` (repository root) | Installs everything that is not committed: a private Python 3.12.10 into `vendor/python`, the pinned LLVM 22.1.0 release into `C4/Frost/vendor/Compiler`, the git submodules and the IED web libraries. Versions and SHA256 sums are pinned in `tools/setup/deps.toml`. Safe to re-run. |
| `Frost/` | The C++ build system plus the C++ support libraries the compiler links (Core, Reflection, ObjectDescription, Profiling, UnitTesting). yaml-cpp (`Frost/Projects/Core/Modules/yaml/yaml-cpp`) and the Tracy client (`Frost/Projects/Profiling/tracy`) are git submodules; glm stays vendored. Its own docs (`Frost/docs/`, `Frost/AGENTS.md`) are separate and describe Frost, not C4. |
| `Frost/vendor/Compiler/` | Clang + LLVM **22.1** for x86_64-pc-windows-msvc (headers, libs, `clang.exe`, `lld-link.exe`), installed by `setup.bat`. Used both to build the compiler and, at run time, by C4.exe to compile `.ll` files and link. Opaque pointers only. When it is missing, `Frost/Build.py` stops and says to run `setup.bat`. |
| `Build.bat` | Runs `Frost/Build.py` with `..\vendor\python\python.exe` (or `python` when that is missing); every argument passes through (`-b`, `-test`, `-c`, `-p`, ...). |
| `Build.py` (C4 root) | The root Frost project: `src/Source.cpp` -> `C4.exe`. |
| `nuke.toml` (C4 root) | The Nuke project `C4` (`kind = "custom"`) of the repository-root workspace: its steps build with Frost, refresh the `dev` toolchain, run the unit tests, clean, and run every gate. |
| `MakeToolchain.py` | Assembles a toolchain from a Frost build ([02-nuke.md](02-nuke.md#5-toolchains)). |
| `GenerateProjects.bat` | `Build.bat -gs -p <C4 root>` - generate the Visual Studio solution. |

`setup.bat` options: `--toolchain` (also build the compiler and the `dev` toolchain), `--with extras` (the optional
packages, among them the Tracy profiler GUI, installed at `vendor/tracy-profiler` at the repository root),
`--add-to-path` (put the repository's `bin/`, which holds the `nuke` launcher, on PATH), `--only NAME`, `--force`
(download and extract again), `--list` (every package and whether it is installed).

### Modules

| Module | Build type | Notes |
|---|---|---|
| root (`src/`) | executable `C4.exe` | the compiler driver; links C4Lib, Frost Core/Reflection/ObjectDescription/Profiling/UnitTesting, every `LLVM*.lib` plus `Remarks.lib` and `LTO.lib`; stages a complete install next to `C4.exe`: `LLVM-C.dll`, `libclang.dll`, `LTO.dll`, `Bedrock/`, `C4Runtime/` and `toolchain/` (clang, lld-link, clang's headers; [01-compiler-cli.md](01-compiler-cli.md#4-the-install)) |
| `C4Lib/` | static library | the compiler itself: Macro, Lexer, Parser, Compiler, SourceManager, Model, Backend/HIR, Backend/CodeGen, Interop, Testing; also includes `C4Runtime/` headers |
| `C4Testing/` | unit-test executable | C++ unit tests of C4Lib (Frost UnitTesting) |
| `C4Protocol/` | static library | JSON values, `Content-Length` framing and the stdio channel the language server and the debug adapter share |
| `C4LSPLib/` | static library | the language server (protocol, session, features); C4LSP and C4Testing link it |
| `C4LSP/` | executable | the language server's stdio loop; in the root module list, so a root build builds it |
| `CBind/` | executable | header binding generator (ClangTool). A root tool (`toolPaths`), built by every root build (EARLY stage). Forced to `-O0` because of an lld COMDAT issue at `-O3` |
| `C4DAPLib/` | static library | the debug adapter (DAP requests and events, the debugger over the Win32 debug API and DbgHelp); C4DAP and C4Testing link it |
| `C4DAP/` | executable | the debug adapter's stdio loop. A root tool (`toolPaths`), built after `C4` |
| `C4Script/` | dynamic library `C4Script.dll` | the script host ([runtime/04-scripting.md](../runtime/04-scripting.md)). A root tool (`toolPaths`), built after `C4` |
| `C4Runtime/` | C/C++ sources, not a module | compiled on demand by C4.exe at build time of each C4 program |

Root module lists (`Build.py`): `modulePaths = C4Lib, C4Testing, C4Protocol, C4LSPLib, C4LSP, C4DAPLib`;
`toolPaths = CBind, C4DAP, C4Script`.
A `toolPaths` entry is an executable of the workspace that builds with the root but contributes no
includes or links to it (a Frost feature: EARLY-stage tools build before the root, others after it).

---

## 2. Commands

First, once per checkout (and again after a dependency pin changes), from the repository root:

```bat
setup.bat
```

The repository root is a Nuke workspace whose default project is `C4`, so from there:

```bat
rem build C4.exe and its tools (Development), then refresh the dev toolchain
nuke build

rem the same in Debug (no toolchain refresh: only a Development build refreshes dev)
nuke build debug

rem build + run the unit tests (C4Testing, CoreTesting, ReflectionTesting); words after -- go to Frost
nuke test
nuke test C4 -- --filter "Lexer.*"

rem build, then run every test suite (05-test-suites.md)
nuke gates

rem build, then install the result as a numbered, immutable toolchain (section 3)
nuke install-c4

rem Frost's -clean
nuke clean
```

Frost can also be run directly from the C4 root (`C:/Work/IED/C4` in these examples; pass your own C4 folder to
`-p`). `Build.bat` passes its arguments to `Frost/Build.py` and runs it with the repository's own Python;
`python` below is any Python 3.12 (`..\vendor\python\python.exe` is the repository's):

```bat
rem incremental build of C4.exe (+ C4LSP, + Bedrock copy next to the exe)
cmd /c "Build.bat -b -c Development -a x64 -p C:/Work/IED/C4"

rem reliable full rebuild (use after deleting/renaming files, stash, or any doubt)
cmd /c "Build.bat -rebuild -c Development -a x64 -p C:/Work/IED/C4"

rem build + run the unit tests of C4Testing, CoreTesting and ReflectionTesting (plain -b never rebuilds
rem them); the exit code is non-zero when any suite fails or crashes
python Frost/Build.py -test -c Development -a x64 -p C:/Work/IED/C4
python Frost/Build.py -test -c Development -a x64 -p C:/Work/IED/C4 --filter "Lexer.*"
```

A direct Frost build does not refresh the `dev` toolchain; `nuke build` (or `MakeToolchain.py`) does.

Frost flags: `-gs` (generate VS solution), `-b` (build), `-fb` (full build), `-rebuild`, `-clean`,
`-r` (run), `-p <path>`, `-c Debug|Development|Dist`, `-a x64`, `-s windows`, `-ci` (compile times),
`-test`, `-testall`, `-benchmark`, `--filter <wildcard>`, `--benchmark-filter <wildcard>`.

When clang or HeaderTool **crashes** (exit 0xC0000005 access violation, 0xC00000FD stack overflow, 0xC0000409 stack
buffer overrun), Frost runs it again, up to twice, and prints `warning: <tool> crashed (...) on <file>; retrying
(n/2)`. clang 22.1.0 and libclang crash intermittently under the parallel build on Windows, on a different file
each time and never reproducibly on a re-run. An ordinary error exit is never retried.

Outputs (in the C4 tree):

```text
bin/Development-windows-x64/C4/C4.exe        compiler, a complete install (+ Bedrock/, C4Runtime/, toolchain/, LLVM dlls)
bin/Development-windows-x64/C4LSP/C4LSP.exe  language server
bin/Development-windows-x64/C4DAP/C4DAP.exe  debug adapter
bin/Development-windows-x64/CBind/CBind.exe  binding generator
bin-int/<config>-windows-x64/<Module>/       object files
```

The test runners and benchmarks expect the compiler at `bin/Development-windows-x64/C4/C4.exe`.

---

## 3. Toolchains from this build

C4 projects do not use `bin/` directly: they build with a toolchain from the shared store
`<repository root>/toolchains/c4/` ([02-nuke.md](02-nuke.md#5-toolchains)).

- **`dev`**: every Development `nuke build` from the repository root runs
  `MakeToolchain.py --config Development --out <store>/c4/dev`, so workspaces pinned to `toolchain = "dev"` build
  with the compiler of this tree's latest Development build.
- **A numbered version**: `nuke install-c4` builds, assembles a toolchain with `MakeToolchain.py` and installs it
  as the version in `C4Lib/src/Compiler/Version.h`. Installed versions never change, so bump the version first.
- **By hand**: `python MakeToolchain.py --out <dir> [--config Development]` assembles a toolchain anywhere, and
  `nuke toolchain install <dir>` copies it into the store.

---

## 4. Build traps checklist

These have each cost real debugging time. Check them before trusting a result.

1. **Swallowed compile errors.** Frost's clang-format step can swallow compiler errors and still report
   "error code 0" / exit 0, leaving a stale binary that looks green. Always grep the full log for
   `: error|error C\d|fatal error`, and confirm the `.obj` of a changed `.cpp` is newer than the source.
2. **Orphan objects.** Frost links every `.obj` in `bin-int/<cfg>-windows-x64/<Module>/`. After renaming or
   deleting a `.cpp`, delete objects that no longer have a source, or the old code silently wins.
3. **Stale incremental builds.** Plain `-b` does not reliably recompile after a delete or stash, and
   content-hash freshness means revert-then-restore does not recompile. Use `-rebuild` for a
   consistent binary; after an A/B experiment check the exe is newer than the changed object.
4. **`-b` needs `-p`.** Without a project path `-b` is a no-op ("A project path is required").
5. **C4Testing is never rebuilt by `-b`/`-fb`.** Use `-test` / `-testall`. `-testall` exits with
   0xC0000005 during ReflectionTesting teardown even when everything passed - read the
   `Total/Passed/Failed` lines.
6. **Stale install.** C4.exe compiles the `Bedrock/` and `C4Runtime/` next to it, which only refresh on a
   real `-b -p` build. Nuke projects use a toolchain from the store: `dev` refreshes only on a Development
   `nuke build` from the repository root (not on a direct Frost build), and a numbered version never changes.
7. **Never kill processes with a pattern like `-match 'C4'`** - it also hits the running C4LSP. Use
   `Get-Process -Name C4` exactly, or move the locked exe aside.
8. **ASCII only** in source and docs. PowerShell `Set-Content -Encoding utf8` prepends a BOM; strip the
   leading bytes 239,187,191 and check that no byte is above 127.

---

## 5. Done means green

A change is finished only when the build succeeds with no swallowed errors and every gate in
[05-test-suites.md](05-test-suites.md) passes (`nuke gates` from the repository root runs them).
