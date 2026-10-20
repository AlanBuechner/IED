# C4.exe - the compiler command line

`C4.exe` compiles a whole program in one shot: every input source plus the Bedrock files it includes is
parsed once, analyzed as one program, lowered to LLVM IR, compiled to an object and linked into an
executable. There is no per-file object or separate link step (see
[language/01-program-model.md](../language/01-program-model.md)).

Built binary: `bin/Development-windows-x64/C4/C4.exe`, a complete install (see section 4 and
[04-building-the-compiler.md](04-building-the-compiler.md)). Nuke projects use the install in their pinned
toolchain (see [02-nuke.md](02-nuke.md#5-toolchains)).

Argument parsing: `C4Lib/src/Compiler/BuildUtils.cpp` (the option table behind `ParseArgs` and
`PrintUsage`). Install lookup: `C4Lib/src/Compiler/Toolchain.cpp`. Driver: `src/Source.cpp`.

---

## 1. Usage

```bat
C4.exe [options] <file-or-folder>...
C4.exe src -o build\Game.exe -O2
C4.exe Bedrock\Tests -o tests.exe -test
```

- Every argument that does not start with `-` is an input. A **folder** is scanned recursively for
  `.cf` and `.ch` files. A single **file** registers its whole parent folder. Every registered folder is
  also an include root.
- `--projects <file>` takes the place of the inputs ([section 5](#5-several-projects-in-one-build)).
- An unknown option, an option without its value, a bad `-O` level or no input at all is a usage error:
  `C4.exe` prints `error: ...` and exits with code 2.

Exit codes: 0 when the program was built (warnings do not fail a build unless `-Werror`), 1 on a
compile or link error, 2 on a usage error. A crash of the compiler itself prints
`C4: internal compiler error: exception 0x... at C4.exe+0x...` and the stack's offsets to stderr;
`llvm-symbolizer` turns them into source lines against the build's `C4.pdb`.

---

## 2. Flags

| Flag | Meaning |
|---|---|
| `-o <path>` | Output executable (default `output.exe`). |
| `-I <dir>`, `-I<dir>` | A project this one builds with: `<dir>` is an input folder (its sources compile into the program) and an include root (its headers resolve, for C4 and for interop headers). This is how one build combines several projects; see section 5. |
| `--projects <file>` | Build the projects a description names, each with its own sources, include folders, defines and dependencies ([language/15-projects.md](../language/15-projects.md)). It replaces the inputs: giving both is a usage error. |
| `-D NAME[=VALUE]`, `-DNAME[=VALUE]` | Define a macro for every file (value 1 when omitted); it may replace a predefined macro ([02-preprocessor](../language/02-preprocessor.md#26-predefined-macros)). |
| `-O<n>`, `-O <n>` | Optimization level 0..3 (default 0), passed to clang when compiling the generated IR. C4 itself has no optimization pass. |
| `-march=<cpu>` | The processor to compile for ([vectors 8](../language/11-vectors-and-swizzling.md#8-the-simd-target), D-62): `x86-64` (the default), `x86-64-v2`, `x86-64-v3`, `x86-64-v4`, any LLVM x86-64 CPU name (`haswell`, `znver4`) or `native` (the building machine). Every function is compiled for it, foreign sources too, and it defines the `SIMD_*` macros. An unknown name is a usage error that lists the levels. |
| `-m<feature>`, `-mno-<feature>` | Turn one processor feature on or off, with what it implies (`-mavx2` turns on `avx`, `-mno-avx` turns off `avx2` and `fma`): `sse3 ssse3 sse4.1 sse4.2 popcnt avx avx2 fma f16c bmi bmi2 lzcnt avx512f avx512vl avx512bw avx512dq avx512cd`. As in clang they apply after the last `-march`, in order. An unknown feature is a usage error that lists them. |
| `-fno-strict-aliasing` | Turn off strict aliasing ([types 4.14](../language/04-types.md#414-strict-aliasing), D-64): the IR carries no type-based alias tags, so any load or store may touch any memory, for code that reads a value through a pointer to another type. |
| `-g` | Debug information: the module describes the program's functions, parameters and locals, types, globals and a line for every statement as CodeView, and the link writes `<base>.pdb` beside the output for the Windows debuggers ([compiler/04-codegen-and-abi.md](../compiler/04-codegen-and-abi.md#debug-information--g)). It combines with every `-O` level. |
| `--int-dir <dir>` | Write the intermediates (`.ll`, `.obj`, `.bc`) in `<dir>`. Without it they go next to the output. |
| `-l <lib>`, `-l<lib>` | Link the library `<lib>` (`<lib>.lib`; a name that already ends in `.lib` is used as is). |
| `-L <dir>`, `-L<dir>` | Look for `-l` libraries in `<dir>` too. |
| `-c` | Compile to `<base>.obj` and stop: no runtime objects, no link. |
| `-bc`, `--emit-bitcode` | Write LLVM bitcode `<base>.bc` and stop. |
| `-S`, `--emit-ir` | Write LLVM IR `<base>.ll` and stop. |
| `-test`, `--test` | Predefine `TEST` and replace the entry point with the generated `[test]` harness. Does not run the exe. See [runtime/03-testing.md](../runtime/03-testing.md). |
| `-test-json <path>` | With `-test`: the test run writes a JSON summary to `<path>` ([testing](../runtime/03-testing.md#3-the--test-build)). |
| `-reflect-tag <tag>` | Source tag for reflection registration (the hot-reload unload key). Empty = the main exe. |
| `--script-host` | Build a script host ([scripting](../runtime/04-scripting.md#3-the-host-build)): the image gets the symbol table scripts link against (`__c4_script_table`) and an accessor per `thread_local`, and `<out>.c4i`, the interface scripts compile against, is written beside the output. A program that uses the scripting API without it is an error that names the flag. |
| `-gc-stats`, `--gc-stats` | The program prints the GC's statistics to stderr after each collection (`[GC] allocs=... collections=... heapBytes=... liveBytes=... freeBytes=... pauseNs=... lastPauseNs=...`). |
| `-Werror` | Every warning is an error: the build fails on the first phase that reports one. |
| `-w` | Warnings are not reported. |
| `-export-macros` | Write each file's final macro table as `<name>.c4macro` next to the output. |
| `-ftime`, `--time-phases` | Print a per-phase timing footer (preprocess, parse, model, lower, object, runtime+link), also when the build succeeds. |
| `--pretty-print` | Dump the parsed ASTs. |
| `-v` | Verbose: prints the tool command lines (clang, CBind, link). |
| `--version` | Print `C4 <major>.<minor>.<patch>` and exit 0. |
| `-h`, `--help` | Print every option and exit 0. |

`<base>` is the output path without its extension, in the `--int-dir` folder when one is given.

---

## 3. What a build does

1. **Register sources**: the inputs and the `-I` folders (or every described project's sources), then the default library at
   `<home>/Bedrock`. Only the Bedrock files the program includes (and what they include) are compiled,
   so a program that includes none of it compiles none of it. A missing `Bedrock/` is an error.
2. **Macro pass**: a text pre-pass per file (`#define`, `#if`, ...). See
   [language/02-preprocessor.md](../language/02-preprocessor.md).
3. **Interop pass**: `extern "c"` / `extern "c++"` blocks. `include` headers go through `CBind.exe`;
   `source` files are compiled with clang into `%TEMP%/c4_interop`, both with the build's `-march` / `-m` flags. See
   [language/13-interop.md](../language/13-interop.md).
4. **Parse** (multithreaded, once per file), **build the ProgramModel**, **lower to HIR and LLVM IR**.
   Any front-end error stops the build. Diagnostics print as `file (line:col) error: message`, followed
   by the source line and a caret; warnings print the same way after a successful build.
5. Write `<base>.ll` (always), and `<base>.bc` for `-bc`.
6. `clang -c -target x86_64-pc-windows-msvc -mstackrealign -march=<cpu> -fexceptions -Wno-override-module -O<n>
   <base>.ll -o <base>.obj` (`-O<n>` is omitted when the level is 0); every function in the IR already carries the
   target's `target-cpu` and `target-features`. `-c` stops here.
7. Compile every C and C++ source of the runtime (`<home>/C4Runtime`: the GC, threads, reflection, tests, EH,
   IO, RTTI, memory) at `-O2` into `%TEMP%/c4_runtime/<hash of the runtime folder>/O2/`
   (`Compiler/RuntimeObjects.cpp`). An object's name carries a key of its source's bytes and every runtime
   header's, so it is rebuilt exactly when one of them changes, whatever the files' times; each is written to a
   private file and renamed into place, so parallel builds never link a half-written object. All of them are linked into **every** exe; a missing runtime folder or a source that does not
   compile is an error.
8. Link with `clang -fuse-ld=lld`: the main object (reflection registration is part of it), the runtime
   objects, foreign objects, `lib(...)` libraries and the `-l` / `-L` arguments, `/SUBSYSTEM:CONSOLE`. With `-g` the link
   also passes `/DEBUG`, which writes `<base>.pdb` beside the exe.

---

## 4. The install

An install is one folder; copying it anywhere gives a working compiler (design D-56). Its folder is the
**home**: `$C4_HOME` when that is set to a folder, else the folder of `C4.exe`.

```text
<home>/
  C4.exe, LLVM-C.dll, libclang.dll, LTO.dll
  Bedrock/                 the default library
  C4Runtime/               the runtime's C and C++ sources
  toolchain/bin/           clang.exe, lld-link.exe
  toolchain/lib/clang/     clang's own headers (stddef.h, stdint.h, intrinsics)
  CBind/CBind.exe          the interop binding generator
```

| Thing | Where it is found |
|---|---|
| Bedrock | `<home>/Bedrock` |
| clang, lld | `<home>/toolchain/bin`; a development build also finds the repository's `Frost/vendor/Compiler/bin` above the executable |
| Runtime sources | `<home>/C4Runtime`; a development build also finds the repository's `C4Runtime/` |
| CBind | `<home>/CBind/CBind.exe`, else `CBind/CBind.exe` beside the executable's folder (the development build's `bin/<config>/CBind/`) |
| Windows SDK and MSVC libraries | found by clang (the newest installed) |

When clang or the runtime is missing, `C4.exe` says which folder it looked in and to set `C4_HOME`.
`Build.py` stages the whole layout into `bin/<config>/C4/`; `MakeToolchain.py` assembles it, with the tools,
docs and skills, into a toolchain ([02-nuke.md](02-nuke.md#5-toolchains)).

Target: **x86_64-pc-windows-msvc only**. Linux support is planned (LongTermPlan item 11) and needs the
GC, the ABI layer and the link step ported.

---

## 5. Several projects in one build

A program is one compile, so a game and the engine it uses build together, with every function visible to the
optimizer across them. There are two ways to say so:

- `C4.exe Game/src -I Engine/src`: the inputs and the `-I` folders are one project, sharing the command line's
  defines and include roots.
- `C4.exe --projects projects.toml -o Game.exe`: a build description names each project's sources, include
  folders, defines and dependencies. Each file is preprocessed with its own project's defines, an include
  resolves from its project and the projects it depends on, and a project cannot reach a project that does not
  serve it ([language/15-projects.md](../language/15-projects.md)). Nuke writes the description for every build.
