# Building the compiler

The C4 compiler, its libraries and tools are C++. They are projects of the IED repository's Nuke workspace, built by
the C/C++ plugin's Nuke driver (`plugins/cpp/nuke/`, [Nuke/README.md section 9c](../../../Nuke/README.md#9c-c-and-c-projects)),
the C++ build system Frost was before it merged into Nuke. C4 programs themselves are built with Nuke's C4 support
(see [02-nuke.md](02-nuke.md)).

The C4 tree is the `C4/` folder of the IED repository; "the repository root" below is the folder above it.

---

## 1. Pieces

| Piece | What it is |
|---|---|
| `setup.bat` (repository root) | Installs everything that is not committed: a private Python 3.12.10 into `vendor/python`, the pinned LLVM 22.1.0 release into `vendor/llvm`, the git submodules and the IED web libraries. Versions and SHA256 sums are pinned in `tools/setup/deps.toml`. Safe to re-run. |
| `vendor/llvm/` (repository root) | Clang + LLVM **22.1** for x86_64-pc-windows-msvc (headers, libs, `clang.exe`, `lld-link.exe`), installed by `setup.bat`; the workspace names it in `[languages.cpp] llvm`. Used both to build the compiler and, at run time, by C4.exe to compile `.ll` files and link. Opaque pointers only. |
| `plugins/cpp/` (repository root) | The C/C++ plugin: its Nuke driver and the C++ libraries the compiler links (`libraries/`: Core with yaml-cpp, Reflection with its HeaderTool and CodeGen, ObjectDescription, Profiling, UnitTesting, ...; their docs in `plugins/cpp/docs/`). yaml-cpp (`plugins/cpp/libraries/yaml/yaml-cpp`) is a git submodule; glm stays vendored. Profiling streams to the IED profiler through the IED Trace Protocol client in `plugins/cpp/libraries/Profiling/src/Profiling/Itp.h`. |
| `nuke.toml` (C4 root) | The Nuke project `C4` (C4.exe and the install beside it); its steps refresh the `dev` toolchain and run every gate. Every other C++ project has its `nuke.toml` in its folder. |
| `MakeToolchain.py` | Assembles a toolchain from the build ([02-nuke.md](02-nuke.md#5-toolchains)). |

`setup.bat` options: `--toolchain` (also build the compiler and the `dev` toolchain), `--with extras` (the optional
packages), `--add-to-path` (put the repository's `bin/`, which holds the `nuke` launcher, on PATH), `--only NAME`,
`--force` (download and extract again), `--list` (every package and whether it is installed).

### Projects

All in the `C4` group, the C4 folder of IED's Solution Explorer:

| Project | Kind | Notes |
|---|---|---|
| `C4` (`src/`) | executable `C4.exe` | the compiler driver; reflects (HeaderTool, CodeGen); links C4Lib, Core/Reflection/ObjectDescription/Profiling/UnitTesting, every `LLVM*.lib` but `LLVM-C.lib`, plus `Remarks.lib` and `LTO.lib`; stages a complete install next to `C4.exe`: `LLVM-C.dll`, `libclang.dll`, `LTO.dll`, `Bedrock/`, `C4Runtime/` and `toolchain/` (clang, lld-link, llvm-rc, clang's headers; [01-compiler-cli.md](01-compiler-cli.md#4-the-install)). Depends on the tools the toolchain ships, so building it builds them |
| `C4Lib` | static library | the compiler itself: Macro, Lexer, Parser, Compiler, SourceManager, Model, Backend/HIR, Backend/CodeGen, Interop, Testing; also includes `C4Runtime/` headers |
| `C4Testing` | test program | C++ unit tests of C4Lib, C4LSPLib and C4DAPLib (the UnitTesting library); depends on `C4`, whose compiler the debug adapter tests launch |
| `C4Protocol` | static library | JSON values, `Content-Length` framing and the stdio channel the language server and the debug adapter share |
| `C4LSPLib` | static library | the language server (protocol, session, features); C4LSP and C4Testing link it |
| `C4LSP` | executable | the language server's stdio loop |
| `CBind` | executable | header binding generator (ClangTool). Built at `-O0` because of an lld COMDAT issue at `-O3` |
| `C4DAPLib` | static library | the debug adapter (DAP requests and events, the debugger over the Win32 debug API and DbgHelp); C4DAP and C4Testing link it |
| `C4DAP` | executable | the debug adapter's stdio loop |
| `C4Script` | DLL `C4Script.dll` | the script host ([runtime/04-scripting.md](../runtime/04-scripting.md)) |

`C4Runtime/` and `Bedrock/` are not C++ projects: C4.exe compiles the runtime's C/C++ sources on demand for each C4
program, and Bedrock is C4.

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

rem build + run the unit tests; the exit code is 1 when any suite fails or crashes
nuke test C4Testing CoreTesting ReflectionTesting
nuke test C4Testing --filter "Lexer.*"

rem build, then run every test suite (05-test-suites.md)
nuke gates

rem build, then install the result as a numbered, immutable toolchain (section 3)
nuke install-c4

rem one project and what it needs; -j caps the compiles running at once
nuke build C4LSP -j 3

rem the configuration's outputs
nuke clean
```

A source compiles again when it, a header it read, its arguments or clang changed; the objects of removed sources
are deleted before the link; a file edited while the build ran compiles again on the next build (Nuke/README.md
section 9c). When clang or the HeaderTool **crashes** (exit 0xC0000005 access violation, 0xC00000FD stack overflow,
0xC0000409 stack buffer overrun), the driver runs it again, up to twice, and prints
`warning: <tool> crashed (...) on <file>; retrying (n/2)`: clang 22.1.0 and libclang crash intermittently under the
parallel build on Windows, on a different file each time and never reproducibly on a re-run. An ordinary error exit is
never retried, and a failed compile stops the build before anything links.

Outputs (in the repository root's `build/`):

```text
build/bin/Development-windows-x64/C4/C4.exe        compiler, a complete install (+ Bedrock/, C4Runtime/, toolchain/, LLVM dlls)
build/bin/Development-windows-x64/C4LSP/C4LSP.exe  language server
build/bin/Development-windows-x64/C4DAP/C4DAP.exe  debug adapter
build/bin/Development-windows-x64/CBind/CBind.exe  binding generator
build/bin/Development-windows-x64/C4Script/        C4Script.dll and its import library
build/int/<config>-windows-x64/<Project>/          object files, dependency files, generated reflection
build/compile_commands.json                        every C++ source's compile, for clangd
```

The test runners and benchmarks expect the compiler at `build/bin/Development-windows-x64/C4/C4.exe`. A development
build finds clang, the runtime and Bedrock beside itself, else in the repository a few folders up (`vendor/llvm`,
`C4/C4Runtime`, `C4/Bedrock`; `Toolchain.cpp`).

---

## 3. Toolchains from this build

C4 projects do not use `build/bin/` directly: they build with a toolchain from the shared store
`<repository root>/toolchains/c4/` ([02-nuke.md](02-nuke.md#5-toolchains)).

- **`dev`**: every Development `nuke build` of `C4` runs
  `MakeToolchain.py --config Development --bin <build/bin/Development-windows-x64> --out <store>/c4/dev`, so workspaces
  pinned to `toolchain = "dev"` build with the compiler of this tree's latest Development build.
- **A numbered version**: `nuke install-c4` builds, assembles a toolchain with `MakeToolchain.py` and installs it
  as the version in `C4Lib/src/Compiler/Version.h`. Installed versions never change, so bump the version first.
- **By hand**: `python MakeToolchain.py --out <dir> [--config Development] [--bin <folder>]` assembles a toolchain
  anywhere, and `nuke toolchain install <dir>` copies it into the store.

---

## 4. Build traps checklist

These have each cost real debugging time. Check them before trusting a result.

1. **Stale install.** C4.exe compiles the `Bedrock/` and `C4Runtime/` next to it, which only refresh when `C4`
   builds. Nuke projects use a toolchain from the store: `dev` refreshes only on a Development `nuke build` of `C4`,
   and a numbered version never changes.
2. **Never kill processes with a pattern like `-match 'C4'`** - it also hits the running C4LSP. Use
   `Get-Process -Name C4` exactly. A running C4.exe, C4LSP or C4DAP does not block a build: the driver moves the
   running file aside (`<file>.<ms>.old`) and links the new one.
3. **ASCII only** in source and docs. PowerShell `Set-Content -Encoding utf8` prepends a BOM; strip the
   leading bytes 239,187,191 and check that no byte is above 127.

What Frost made you check by hand - errors reported as success, orphan objects linked, `-b` not recompiling after a
delete, test programs never rebuilt - the driver does not do: a failed compile fails the build, removed sources'
objects go before the link, every source's dependencies are checked on every build, and `nuke test` builds the
test programs first.

---

## 5. Done means green

A change is finished only when the build succeeds and every gate in [05-test-suites.md](05-test-suites.md) passes
(`nuke gates` from the repository root runs them).
