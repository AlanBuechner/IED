# Building C4 projects with Nuke

Nuke builds, runs and tests C4 workspaces. It lives in the IED repository (`Nuke/` at its root) and runs as
`nuke`: the launcher is `bin/nuke.cmd` at the repository root, which `setup.bat --add-to-path` puts on PATH
(`nuke.bat` at the root works too). The command line, the `nuke.toml` manifests, build steps, custom commands
and extension scripts are documented in the Nuke reference, [Nuke/README.md](../../../Nuke/README.md) in the IED
repository; the design is [docs/23-nuke-build-system.md](../../../docs/23-nuke-build-system.md) there. This page
keeps what is specific to C4: the C4.exe command line Nuke builds, when a program is rebuilt, tests, the editor
bridge and the versioned toolchains.

---

## 1. A C4 workspace

```text
MyGame/
  nuke.toml             [workspace]: name, toolchain, default-project
  Game/
    nuke.toml           [project]: kind = "executable", depends = ["Engine"]
    src/                .cf/.ch sources (compiled as one program)
    src/tests/          optional [test] sources; makes the project test-bearing
  Engine/
    nuke.toml           [project]: kind = "module"
    src/
  .ied/skills/          AI skills (committed)
  .ied/local/           machine-local files Nuke writes for the editor (gitignored)
  build/bin/...         outputs (gitignored)
  build/int/...         intermediates (gitignored)
```

| `kind` | What Nuke does with it |
|---|---|
| `executable` | a program: one compile of its sources and of every module it depends on |
| `module` | source only: compiled into each program that depends on it, and on its own only for its tests |
| `custom` | no C4 compile; its `[[project.steps]]` do the work (the compiler's own `C4/nuke.toml` is one) |

`nuke init` in an empty folder writes a one-project workspace (`nuke.toml` with both tables, `src/main.cf` for an
executable, a `.gitignore` with `build/` and `.ied/local/`) and copies the toolchain's language skills (`c4-language`,
`c4-testing`, `c4-reflection`, `c4-garbage-collector`) and Nuke's own skill into `.ied/skills/`.

### Configuration -> C4.exe flags

| Config | Command word | C4.exe flags |
|---|---|---|
| Debug | `debug` | `-O0 -g` |
| Development (default) | `dev` | `-O3 -g` |
| Dist | `dist`, `release` | `-O3` |

`-g` writes a `.pdb` beside the output, so a debugger can attach to Debug and Development builds; Dist is the
shipping build and has none. A workspace's `[configurations.<Name>]` tables change these or add configurations;
their `defines` reach C4.exe as `-D NAME=VALUE` and apply to every file of the program.

---

## 2. How a program is compiled

C4 has no libraries: a program is one compile. For each executable (and, under `nuke test`, each test-bearing
project) Nuke runs, from the project folder:

```bat
<toolchain>\C4.exe --projects <int>/projects.toml -O<n> [-g] -D <configuration define>... -l <link>...
    <compiler-args> --int-dir <int> -o <bin>/<Project>.exe
```

`<bin>` is `build/bin/<Config>-windows-x64/<Project>/` and `<int>` is `build/int/<Config>-windows-x64/<Project>/`,
both at the workspace root.

- `projects.toml` (written into `<int>`; [../language/15-projects.md](../language/15-projects.md)) lists the
  program and every C4 module it depends on, directly or not: each one's `sources` (default `["src"]`),
  `includes`, `defines` (plus `<CONFIG> = 1`, for example `DEVELOPMENT`, and `PROJECT_NAME`) and `depends`. So
  each module's files get its own defines, and a module cannot include or name anything of the project that
  depends on it. Every function is still visible to the optimizer across modules: the program is one compile.
- The `links` of the program and of every module compiled into it (`"winmm.lib"`) become `-l` arguments; a
  module's links reach every program it is compiled into.
- The program's `compiler-args` (and those of its `[project.config.<Config>]` override) go on the command line.
  The processor goes here: `compiler-args = ["-march=x86-64-v3"]`, which the editor bridge passes on as `target`.
- The compiler's intermediates (the object cache `<name>.objects/`, foreign objects in `<name>.interop/`) go to `<int>`, the executable and its `.pdb` to `<bin>`; then the
  `[[project.copy]]` entries of the program and its modules are copied into `<bin>`.
- A dependency cycle is an error that prints the chain.

### When a program is rebuilt

A program is compiled again when:

- its output is missing;
- the command line or the build description changed;
- the set of tracked files changed (a file was added or deleted);
- any tracked file is newer than the output.

The tracked files are every file under the sources and includes of the program and its modules, the toolchain's
`C4.exe`, and the toolchain's `Bedrock/` and `C4Runtime/`, so a new compiler in the toolchain rebuilds the
programs that use it. The stamps (`compile.args`, `deps.d` in `<int>`) are written only after a successful
compile: a failed compile is never taken for an up-to-date one, and `nuke run` never starts an older executable.

---

## 3. Tests

A C4 project is test-bearing when one of its source folders has a `tests/` folder with `.cf` or `.ch` files
(`src/tests/` by default), or its manifest sets `tests = true` (`tests = false` turns the detection off).

- `nuke build` and `nuke run` never pass `-test`. `src/tests/` is under `src/`, so its files are part of the
  program, where the `[test]` functions are inert code; an executable still needs its `main`.
- `nuke test` compiles each test-bearing project on its own with `-test -test-json <int>/tests/results.json`
  into `<bin>/<Project>-tests.exe` (intermediates in `<int>/tests/`, same rebuild rules), runs it from the project
  folder (with `--filter GLOB` when given), runs every harness even after one fails, prints a summary, and exits
  1 if any failed. Without project names it tests every test-bearing project.
- A module with tests is compiled with the modules it depends on and needs no `main`.

See [runtime/03-testing.md](../runtime/03-testing.md).

---

## 4. The editor bridge

After every successful build of an executable (and every test build of a module) Nuke writes
`<workspace>/.ied/local/lsp-options/<Project>.json`, the options IED passes to the C4 language server:

| Key | Value |
|---|---|
| `includePaths` | the source and include folders of the program and its modules |
| `defines` | the program's defines (with `<CONFIG>` and `PROJECT_NAME`) |
| `c4Home` | the toolchain folder, whose Bedrock the server compiles with |
| `toolchain` | the toolchain's version |
| `projects` | the `projects.toml` of section 2 |
| `target` | the `-m...` compiler arguments, so the editor sees the build's `SIMD_*` macros |

The file holds absolute paths, so it is machine-local. A workspace that sets `ide = false` gets none. See
[03-editor-lsp-dap.md](03-editor-lsp-dap.md).

### Debugging

Debug and Development builds have a `.pdb` beside the exe. The toolchain's `tools/C4DAP/C4DAP.exe` debugs them for
any Debug Adapter Protocol client over stdio (adapter id `c4`): breakpoints, stepping, the call stack, values by
their C4 types, watch and hover expressions, exception stops and pause
([03-editor-lsp-dap.md](03-editor-lsp-dap.md#3-c4dap-debugger)). Under a debugger an uncaught exception stops at
its throw, and the program then ends with the exception's code (0xE06D7363) instead of the runtime's exit code 1.

---

## 5. Toolchains

A toolchain is a complete C4 install ([01-compiler-cli.md](01-compiler-cli.md#4-the-install)) with the tools,
docs and skills of that compiler. Toolchains live in a shared store, `<IED repo>/toolchains/c4/<version>/`
(the environment variable `NUKE_TOOLCHAINS` names another store root). A workspace picks one with
`[workspace] toolchain`:

| `toolchain =` | Meaning |
|---|---|
| `"0.1.0"` | a numbered install; installed versions never change |
| `"dev"` | the compiler built from the IED repository's `C4/` by its latest Development build |
| `"latest"` | the newest numbered install |
| a folder path | any install folder |

`nuke toolchain list`, `which`, `install`, `remove` and `use` manage the store and the workspace's pin (see the
Nuke reference).

### The layout

`C4/MakeToolchain.py --out <dir> [--config Development]` assembles a toolchain from a Frost build of the compiler
(`C4/bin/<Config>-windows-x64/`):

```text
<toolchain>/
  toolchain.json        name, version (from C4.exe --version), git commit, dirty flag, configuration, build time
  C4.exe, *.dll, *.pdb  the compiler and the LLVM dlls it loads
  Bedrock/              the default library
  C4Runtime/            the runtime's C and C++ sources
  toolchain/bin, lib/   clang, lld-link, clang's headers
  CBind/                the binding generator
  C4Script/             the script host library (runtime/04-scripting.md)
  tools/C4LSP/          the language server (+ the LLVM dlls it loads)
  tools/C4DAP/          the debug adapter
  docs/                 the user-facing language, library, runtime and tools docs (this page among them)
  skills/               the AI skills for this version of the language (from C4/skills/)
```

The folder is mirrored: unchanged files are not copied again and files gone from the build are removed.

### Making and installing one

From the IED repository root, a workspace whose project `C4` (`C4/nuke.toml`) builds the compiler
([04-building-the-compiler.md](04-building-the-compiler.md)):

- `nuke build` builds the compiler with Frost and, for Development, refreshes the `dev` toolchain
  (`MakeToolchain.py --out <store>/c4/dev`). A workspace pinned to `dev` uses the new compiler on its next build.
- `nuke install-c4` builds, then installs the result as a numbered toolchain whose version comes from
  `C4/C4Lib/src/Compiler/Version.h`. Installed versions never change: bump the version first. A workspace then
  pins it with `nuke toolchain use <version>`.

Note: `C4/skills/*/SKILL.md` are short agent-facing summaries kept in sync with these docs (they are the
compressed view; when the two disagree, these pages win). Every toolchain ships them in `skills/`.

Gaps:

| Gap | Status |
|---|---|
| Static/dynamic library outputs. | **Planned** (needs compiler support) |
| Linux. | **Planned** |
