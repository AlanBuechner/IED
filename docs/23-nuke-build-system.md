# 23 - Nuke build system and versioned toolchains

## Purpose

Why Nuke is built the way it is, what it guarantees, and what is left to do. The user reference is
[`Nuke/README.md`](../Nuke/README.md); how IED drives it (tasks, problem matchers, the Error List) is
[`06`](06-build-and-nuke-integration.md). Decided 2026-10-20 when the C4 language moved into this repository.

## Goals

1. **One copy for every workspace.** A project never vendors Nuke or the compiler. Nuke lives in this
   repository; workspaces name a toolchain version and share the installs built here.
2. **Projects add their own build work.** Cooking assets, generating code, kitting a shippable folder,
   deploying: declared steps on well-defined phases, plus commands of their own - without editing Nuke.
3. **A readable, checkable project model.** IED reads workspaces without running code; a typo in a manifest
   is an error naming the file and key, never a silently different build.
4. **The fix-and-continue loop.** An agent that finds a compiler bug while working in any workspace fixes it in
   `C4/`, rebuilds, and continues with the fixed compiler at once (`toolchain = "dev"`), or freezes it as a new
   version.
5. **Usable by this repository.** IED's own build (the C4 compiler today, the C4 server later) is a Nuke workspace.

## Decisions

| # | Decision | Why |
|---|---|---|
| N-1 | Manifests are TOML (`nuke.toml`), parsed by stdlib `tomllib`; Python only through explicit `scripts` (`register(nuke)`) and `python = "file.py:function"` steps | IED and tools can read a workspace without executing it; the escape hatch stays for real logic. The old `Project.py` manifests needed Python to even list projects |
| N-2 | Strict validation with suggestions (unknown keys, types, phases, dependencies, cycles) | Goal 3; agents get an actionable message instead of a wrong build |
| N-3 | Project kinds: `executable`, `module`, `library` (another language's, N-15), `custom` | C4 compiles a program in one piece, so a module is source folded into programs (C4 has no libraries); `custom` covers everything else (web assets, tools) through steps; C++ is a language of its own since N-17 |
| N-4 | Every command has `pre-X`, `X`, `post-X` phases; per project in dependency order, then workspace-level; each (command, project) once per invocation | One rule for built-in and user commands; `nuke build run` and `requires = ["build"]` never build twice |
| N-5 | Commands chain on the command line; words are commands, then configurations, then projects | `nuke build run dev`, `nuke kit dist`, `nuke test Engine` - the owner's requested form, without flags |
| N-6 | Steps with `outputs` are incremental (outputs present, inputs as they were when the last good run began, the expanded command unchanged) | Cooking and codegen steps must not cost a full rerun on every build |
| N-7 | Toolchain store at `<repo>/toolchains/c4/<version>/`, immutable versions, a mutable `dev` entry, `latest`, or a folder path; `NUKE_TOOLCHAINS` overrides | Goal 1 and 4; immutability makes a pinned workspace reproducible; `dev` closes the loop with no extra step |
| N-8 | `C4/MakeToolchain.py` (C4-side) assembles an install; `nuke toolchain install` (Nuke-side) only copies an install into the store | The compiler knows its install layout; Nuke knows the store. The C++ driver stays a plain C++ build (the old `-kit` step lived in Frost); MakeToolchain reads the build's `--bin` folder |
| N-9 | Tests compile into a separate `<P>-tests.exe` with `-test -test-json`; a plain build never includes the harness | The old Nuke passed `-test` on every build of a test-bearing project, so `run` ran tests instead of the program |
| N-10 | Incremental C4 compile also tracks the set of files and the toolchain's Bedrock/C4Runtime; each input's mtime and size are taken before the compile and stamped only after success, and the next build compares them, not the output's mtime | The old rules missed a newly added file and could call a failed compile up to date; comparing with the output's mtime called a source edited during a build (several sessions share one checkout) up to date when the build finished after the edit |
| N-11 | The IDE bridge writes `.ied/local/lsp-options/<P>.json` after each compile, also one that fails | Replaces the predecessor's per-workspace LSP options file; same content, IED's location (`03`); the c4 profile hands every one to one C4LSP (`docs/24` "Options files") |
| N-12 | `nuke` on PATH through `bin/nuke.cmd` (and `bin/nuke` for POSIX shells), offered by `setup.bat` | A global command like the owner asked for, without installing anything outside the repository |
| N-13 | Exit codes: 1 for Nuke-level errors, otherwise the failing tool's or program's exact code (32-bit crash codes included) | Scripts, IED tasks and agents can tell "your manifest is wrong" from "your program returned 3" |
| N-15 | Other languages build through **drivers** in their IED language plugins (IED `docs/26` section 6): a project names its `language`, the package comes from `[languages]`, the workspace's `.ied/plugins/` or IED's plugins, and its Python driver runs the native tool (cargo, go) and returns the artifacts Nuke copies into `build/bin`. Native manifests, lockfiles and caches are never written or moved | Native first (owner, 2026-10-21): a Rust or Go project builds the way its developers build it outside IED; Nuke adds only the cross-language graph, configurations and one output layout. A project's own language plugin can carry its driver |
| N-14 | Debug and Development compile with C4's `--incremental`, Dist without; a configuration sets it with `incremental = true/false`, and Nuke passes it only when the toolchain's `-h` lists it | The edit loop of a large program (the IED server) must not pay a whole-program `-O3` optimize on every change: a one-module edit of the 60k-line ScaleTest rebuilds in 2.6 s instead of 12.3 s, at the same run speed; the shipped build keeps cross-object inlining (C4 D-76). Older pinned toolchains keep working |
| N-16 | A C4 output that is running is renamed aside (`<output>.<ms>.old`) before the compile, and old copies are removed when nothing runs them | A running server under test, or an IDE run from the checkout, locked `ied-server.exe` and failed every other build with "permission denied" at link time - including the IDE's own Rebuild (`docs/28`) |
| N-17 | C and C++ build through the **C/C++ plugin's driver** (`plugins/cpp/nuke/driver`, Nuke/README.md section 9c), the build Frost was: clang `-cc1` per source for the MSVC ABI, lld / llvm-lib / `-shared` links, the Reflection library's HeaderTool and CodeGen; Frost's C++ libraries (Core, Reflection, UnitTesting, ...) are the package's library projects (`plugins/cpp/libraries`). The C4 compiler and its tools are ten `cpp` projects in the `C4` group; Frost, its `Build.py` scripts and `C4/bin` are gone | One build system (owner, 2026-10-21): C++ joins C4, Rust and Go under one command, one manifest format, one output layout and one IDE model, and each C++ project of the C4 tree is a project of its own in IED. Static libraries hold their own objects (Frost's archives merged every library they linked: C4LSPLib.lib was 1 GB); a program links the static libraries it reaches and their links. An object is stamped with its compile's start, so an edit during a build is never called up to date (N-10's rule for C++) |
| N-18 | A language package extends Nuke through data and its driver: `[languages.<id>]` tables of the driver's workspace settings, library projects in its description (joining a workspace that depends on one by name), `build-before-test`, a driver that is a Python package, `implicit_depends`, `${<language>.<name>}` variables, and a context with the project's dependencies, settings, variables and `-j` | Owner: C++ comes through "the ability for a language plugin to extend the nuke build system", so Nuke itself stays language-neutral beside C4 and another language (a workspace's own) can do the same |
| N-22 | `nuke project add FOLDER`, `nuke project remove NAME [--deleting]`, `nuke project move PROJECT TO` change a workspace's projects in its manifests (text edits that keep comments and layout, `nuke/tomledit.py`): add writes the folder's `[project]` (the kind from what is there: a C4 folder with a `main.cf` an executable, without one a module, a language with a driver an executable, any other custom) and lists it; remove drops its `projects` entry or excludes it (refused while another project depends on it, or for the workspace's own project); move moves the folder - or, when an IDE moved it already, only fixes the entries - and names manifest paths that climb out of it. `exclude` now also filters what a `projects` glob finds | Owner (2026-10-22): AI tool calls that make, move and remove projects and make a folder a project, shown live in IED's Solution Explorer; IED's `nuke` project system runs these, then `nuke init --ied` (IED `docs/03` "Managing projects") |
| N-20 | `nuke init --language ID` names the project's language in its manifest: another language than C4 gets no sources (the IDE template that asked brings them) and pins a toolchain only when one is installed; a C4 `module` is refused for another language (it is a `library` there) | Owner (2026-10-22): New Project templates for C/C++, C4 and Go, each language plugin's own; the build system's init makes every one of them, so IED writes no manifest itself (`docs/03` "New projects") |
| N-19 | `nuke init --ied` writes the solution's `groups` (the Solution Explorer's folders) from the projects' `group`, `/` nesting, sorted by name; library projects outside the workspace get no project file | Owner: the C4 projects grouped in one folder; the solution file is generated, so the grouping lives in the manifests that are not |
| N-21 | `nuke init`'s C4 executable is a starting point to build on: `src/main.cf` reads its arguments and writes through `Core::Format` (out through `printf`, which every installed toolchain has), and `src/tests/main_tests.cf` holds an example `[test]`; a fresh project builds, runs (exit 0 with no arguments) and tests | The language eval (`tools/ai-evals/lang`, variant v6): a local model starting from it wrote idiomatic C4 sooner than from a one-line `printf` |

## Not done yet (tracked in WORK.md)

- `--diagnostics-json` with absolute paths (N5) needs compiler support; until then IED's problem matcher reads
  the text output (`06`).
- Parallel builds of independent projects; today projects build one after another (each C4 compile is
  already one whole-program compile).
- Downloading published toolchains (for machines that do not build the compiler) - a GitHub release per
  numbered version is the planned source; the store layout already matches.
- Other systems and architectures: the configuration model has the slots (`-a`, `-s`), only x64 Windows exists.
