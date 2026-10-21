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
| N-3 | Project kinds: `executable`, `module`, `library` (another language's, N-15), `custom` | C4 compiles a program in one piece, so a module is source folded into programs (C4 has no libraries); `custom` covers everything else (the C++ compiler via Frost, web assets, tools) through steps |
| N-4 | Every command has `pre-X`, `X`, `post-X` phases; per project in dependency order, then workspace-level; each (command, project) once per invocation | One rule for built-in and user commands; `nuke build run` and `requires = ["build"]` never build twice |
| N-5 | Commands chain on the command line; words are commands, then configurations, then projects | `nuke build run dev`, `nuke kit dist`, `nuke test Engine` - the owner's requested form, without flags |
| N-6 | Steps with `outputs` are incremental (inputs older than outputs and the expanded command unchanged) | Cooking and codegen steps must not cost a full rerun on every build |
| N-7 | Toolchain store at `<repo>/toolchains/c4/<version>/`, immutable versions, a mutable `dev` entry, `latest`, or a folder path; `NUKE_TOOLCHAINS` overrides | Goal 1 and 4; immutability makes a pinned workspace reproducible; `dev` closes the loop with no extra step |
| N-8 | `C4/MakeToolchain.py` (C4-side) assembles an install; `nuke toolchain install` (Nuke-side) only copies an install into the store | The compiler knows its install layout; Nuke knows the store. Frost stays a plain C++ build system (the old `-kit` step lived in Frost) |
| N-9 | Tests compile into a separate `<P>-tests.exe` with `-test -test-json`; a plain build never includes the harness | The old Nuke passed `-test` on every build of a test-bearing project, so `run` ran tests instead of the program |
| N-10 | Incremental C4 compile also tracks the set of files and the toolchain's Bedrock/C4Runtime; stamps only after success | The old rules missed a newly added file and could call a failed compile up to date |
| N-11 | The IDE bridge writes `.ied/local/lsp-options/<P>.json` after each compile | Replaces the predecessor's per-workspace LSP options file; same content, IED's location (`03`) |
| N-12 | `nuke` on PATH through `bin/nuke.cmd` (and `bin/nuke` for POSIX shells), offered by `setup.bat` | A global command like the owner asked for, without installing anything outside the repository |
| N-13 | Exit codes: 1 for Nuke-level errors, otherwise the failing tool's or program's exact code (32-bit crash codes included) | Scripts, IED tasks and agents can tell "your manifest is wrong" from "your program returned 3" |
| N-15 | Other languages build through **drivers** in their IED language plugins (IED `docs/26` section 6): a project names its `language`, the package comes from `[languages]`, the workspace's `.ied/plugins/` or IED's plugins, and its Python driver runs the native tool (cargo, go) and returns the artifacts Nuke copies into `build/bin`. Native manifests, lockfiles and caches are never written or moved | Native first (owner, 2026-10-21): a Rust or Go project builds the way its developers build it outside IED; Nuke adds only the cross-language graph, configurations and one output layout. A project's own language plugin can carry its driver |
| N-14 | Debug and Development compile with C4's `--incremental`, Dist without; a configuration sets it with `incremental = true/false`, and Nuke passes it only when the toolchain's `-h` lists it | The edit loop of a large program (the IED server) must not pay a whole-program `-O3` optimize on every change: a one-module edit of the 60k-line ScaleTest rebuilds in 2.6 s instead of 12.3 s, at the same run speed; the shipped build keeps cross-object inlining (C4 D-76). Older pinned toolchains keep working |

## Not done yet (tracked in WORK.md)

- `--diagnostics-json` with absolute paths (N5) needs compiler support; until then IED's problem matcher reads
  the text output (`06`).
- Parallel builds of independent projects; today projects build one after another (each C4 compile is
  already one whole-program compile).
- Downloading published toolchains (for machines that do not build the compiler) - a GitHub release per
  numbered version is the planned source; the store layout already matches.
- Other systems and architectures: the configuration model has the slots (`-a`, `-s`), only x64 Windows exists.
