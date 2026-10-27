# Nuke

Nuke is the build system for C4 workspaces: it compiles C4 programs with a versioned C4 toolchain, runs them
and their tests, and runs whatever else a project needs around the compile - cooking assets, generating
code, packaging a shippable folder - as declared steps and commands. It is stdlib-only Python (3.11+) and
lives in this repository; every workspace on the machine uses this one copy and the toolchains built here.
Design and decisions: [`../docs/23-nuke-build-system.md`](../docs/23-nuke-build-system.md).

- [1. Getting it](#1-getting-it)
- [2. The command line](#2-the-command-line)
- [3. Workspaces and projects](#3-workspaces-and-projects)
- [4. Steps](#4-steps)
- [5. Commands of your own](#5-commands-of-your-own)
- [6. Python extensions](#6-python-extensions)
- [7. Variables](#7-variables)
- [8. Toolchains](#8-toolchains)
- [9. C4 projects: compiling, tests, the IDE bridge](#9-c4-projects-compiling-tests-the-ide-bridge)
- [9b. Projects in other languages](#9b-projects-in-other-languages)
- [9c. C and C++ projects](#9c-c-and-c-projects)
- [10. This repository as a workspace](#10-this-repository-as-a-workspace)
- [11. Layout of Nuke itself](#11-layout-of-nuke-itself)
- [12. Packages](#12-packages)

---

## 1. Getting it

`setup.bat` at the repository root installs the private Python Nuke runs on (`vendor/python`). Then:

- `bin/nuke.cmd` (and `bin/nuke` for POSIX shells) is the launcher. `setup.bat --add-to-path` (or answering
  yes when it asks) puts `bin/` on your user PATH so `nuke` works in any folder, also run by a quoted name as IED's
  tasks run it (`"nuke" build ...`, which gives a batch file's top level the wrong folder for `%~dp0`).
- `nuke.bat` at the repository root does the same without PATH.
- A workspace needs no copy of Nuke or of the compiler: it names a toolchain version (section 8).

## 2. The command line

```text
nuke [COMMAND...] [CONFIG] [PROJECT...] [options] [-- PROGRAM ARGS]
```

Words are read left to right. A word is a **command** if one has that name, else a **configuration**, else
a **project** of the workspace. Commands run in order and each (command, project) pair runs once, so
`nuke build run dev` builds once and then runs. With no command, Nuke builds.

| Command | Does |
|---|---|
| `build` | build the project (default: the workspace's default project) and everything it depends on |
| `run` | `build`, then run the project's program; exits with the program's exit code; words after `--` go to it |
| `test` | build and run the tests of the named projects (default: every project that has tests); `--filter GLOB` |
| `clean` | delete this configuration's outputs of the named projects and their dependencies (default: all) |
| `rebuild` | `clean`, then `build` |
| `list` | the workspace's projects (`--json`: name, kind, folder, manifest, depends, tests, default, output) |
| `info` | workspace, configuration, toolchain, output folders and commands (`--json`) |
| `init` | a new workspace here, or a new project inside the current workspace (`nuke init --help`; a C4 executable gets `src/main.cf` and an example test, `src/tests/main_tests.cf`); with `--ied` also the workspace's IED solution (below); `--language ID` for a project in another language than C4 (no sources: IED's New Project templates bring them) |
| `project` | `add FOLDER` (an existing folder made a project: its `[project]` written when it has none, the workspace listing it), `remove NAME` (out of the workspace, its folder kept: its `projects` entry goes or `exclude` takes it; refused while another project depends on it; `--deleting` when the caller deletes the folder next), `move PROJECT TO` (the folder moved and the workspace's entries fixed, or only the entries when it was moved already); manifests are edited in place, their comments kept; `--ied` writes the IED solution again (`nuke project --help`) |
| `toolchain` | `list`, `which`, `install`, `remove`, `use` (section 8) |
| `help` | every command, including the workspace's own; `nuke help COMMAND` |

`init`, `project`, `toolchain` and `help` take the rest of the line as their own arguments.

| Option | Meaning |
|---|---|
| `-p, --project NAME` | work on NAME (repeatable; same as naming it as a word) |
| `-c, --config NAME` | configuration (same as the word) |
| `-a, --arch NAME` / `-s, --system NAME` | `x64` / `windows` (the only ones today) |
| `-w, --workspace DIR` | the workspace at DIR instead of searching up from the current folder |
| `--all` | every project of the workspace |
| `--filter GLOB` | test cases to run (`*` any run of characters, `?` one) |
| `-j, --jobs N` | how many compiles a language driver runs at once (C++, section 9c; default `NUKE_JOBS`, else one per processor) |
| `--json` | machine-readable output for `list` and `info` (messages go to stderr) |
| `-v, --verbose` | print every command Nuke runs |
| `--version` | Nuke's version |

**Configurations.** Built in: `Debug` (`debug`; `-O0 -g`), `Development` (`dev`; `-O3 -g`; the default) and
`Dist` (`dist`, `release`, `ship`; `-O3`, no debug information). Debug and Development compile with C4's
`--incremental` (each object optimized alone, so an edit reoptimizes only the objects it changes; small functions
still inline across objects), when the pinned toolchain has it; Dist optimizes the whole program as one. Names and
aliases are case-insensitive. A workspace adds or changes them (section 3).

**Exit codes.** 0 on success; 1 for a problem with the command line, a manifest or the workspace; otherwise
the exit code of the compile, step or program that failed (`run` passes a crash code like `0xC0000005`
through unchanged).

## 3. Workspaces and projects

A **workspace** is a folder whose `nuke.toml` has a `[workspace]` table. Nuke finds it by searching up from
the current folder. A **project** is a folder with a `nuke.toml` holding a `[project]` table. One file may
hold both (a one-project workspace; `nuke init` writes that). A project-only `nuke.toml` with no workspace
above it is a workspace of its own.

```toml
# nuke.toml at the workspace root
[workspace]
name = "MyGame"                    # default: the folder name
toolchain = "0.1.0"                # section 8; "latest" or unset = the newest installed (with a warning)
default-project = "Game"           # what build/run/test use when no project is named
projects = ["Game", "Engine", "tools/*"]   # folders or globs; leave out to find every project below
exclude = ["samples/**"]           # folders to skip: in discovery, and among what a projects glob finds
default-configuration = "Development"
scripts = ["build/nuke_ext.py"]    # Python extensions (section 6)
ide = true                         # write the IDE bridge files (section 9)
ied-plugins = ["plugins/*"]        # IED plugins it develops: projects of its IED solution (section 9)

[configurations.Profile]           # add a configuration, or change a built-in one
optimize = 2                       # 0..3
debug-info = true
incremental = false                # C4's --incremental (default: true for Debug and Development)
aliases = ["prof"]
defines = { PROFILE = "1" }        # passed to every C4 compile of this configuration

[commands.kit]                     # section 5
description = "Package a shippable folder"
requires = ["build"]

[languages]                        # section 9b: where a language's driver comes from (default: the workspace's
dialogue = "tools/dialogue"        # .ied/plugins/, then IED's own plugins beside Nuke - rust, go, cpp)

[languages.cpp]                    # or a table: what the language's driver reads (section 9c), with
llvm = "vendor/llvm"               # package = "<folder>" when it also pins the package
```

Discovery (no `projects` list) walks the workspace and skips `build`, `bin`, `bin-int`, `vendor`,
`third_party`, `toolchains`, `node_modules`, hidden folders, `exclude` matches, and any folder that is a
workspace of its own.

```toml
# Game/nuke.toml
[project]
name = "Game"                      # default: the folder name
kind = "executable"                # executable | module | library | custom
description = "The game"
group = "Games"                    # the folder IED's Solution Explorer shows it in ("Engine/Tools" nests)
language = "c4"                    # default "c4" for executable and module; another one builds through its driver
                                   # (section 9b); a custom project's is for IDEs only
depends = ["Engine"]               # build order; C4 programs also compile their modules' sources
sources = ["src"]                  # folders or files of .cf/.ch sources (default ["src"])
includes = ["third_party/include"] # further include folders
defines = { GAME_EDITOR = "1" }    # this project's own macros (modules keep theirs)
links = ["user32", "winmm.lib"]    # system libraries; a module's links reach every program it is compiled into
compiler-args = ["-march=x86-64-v3"]
subsystem = "console"              # a C4 executable: "console", or "windows" for a windowed program (no console window;
                                   # C4.exe --subsystem; test harnesses stay console)
tests = "auto"                     # "auto": a tests/ folder with .cf/.ch under a source folder; or true/false
working-dir = "."                  # where `nuke run` and the tests start, relative to the project
scripts = ["nuke_ext.py"]          # extensions whose steps default to this project

[[project.copy]]                   # copied into the program's bin folder after each build (when newer)
from = "assets"                    # file or folder, relative to the project
to = "assets"                      # relative to the bin folder (default: the source's name)
exclude = ["nuke.toml"]            # of a folder: paths in it or names (globs) left out - and removed from a mirror

[project.config.Dist]              # per-configuration additions
defines = { SHIP = "1" }
links = ["dbghelp"]
compiler-args = []

[[project.steps]]                  # section 4
phase = "pre-build"
run = ["${python}", "tools/cook.py"]

[project.driver]                   # a project in another language: what its driver reads (section 9b)
package = "./cmd/server"
```

| Kind | Builds | Runs |
|---|---|---|
| `executable` | a C4 program `<Name>.exe` from its sources plus the sources of every C4 module it depends on; in another language, its driver's build (`cargo build`, `go build`) with the artifacts copied into its bin folder | the `.exe`, or the program its driver names |
| `module` | nothing on its own: its sources compile into the programs that depend on it; its tests build a harness | - |
| `library` | a library in another language than C4 (a C4 one is a module), through its driver | - |
| `custom` | only what its steps do (any language, any tool); `depends` orders it | its `output = "..."`, or its `run` steps |

Manifests are checked strictly: an unknown key, a wrong type, an unknown project in `depends`, a dependency
cycle, a C4 program depending on another C4 executable, a `module` in another language or a `library` in C4,
or a step in an unknown phase stops Nuke with the file, the key and a suggestion.

## 4. Steps

A **step** is work attached to a **phase**. Every command `X` - built-in or your own - has the phases
`pre-X`, `X` and `post-X`, and runs, for each project it works on (dependencies first):

```text
workspace pre-X steps
for each project:  pre-X steps  ->  X's built-in work  ->  X steps  ->  post-X steps
workspace X steps  ->  workspace post-X steps
```

`build`'s built-in work is the C4 compile and the copies; `run`'s starts the program; `test`'s builds and runs
the harness; `clean`'s deletes the outputs. Custom projects have no built-in work, so their `build` steps are
their build.

```toml
[[project.steps]]
name = "cook-textures"             # default: the program's file name
phase = "pre-build"
description = "Compress textures"
run = ["${python}", "tools/cook.py", "${project.dir}/assets", "${int}/cooked"]   # argv (preferred) ...
# run = "cook.bat assets"          # ... or a command line for the shell
# python = "tools/cook.py:cook"    # ... or a function(ctx) in a Python file (or "module:function")
cwd = "tools"                      # default: the project folder (workspace folder for workspace steps)
env = { COOK_LEVEL = "high" }
inputs = ["assets/**/*.png"]       # with outputs: the step is skipped while every output exists, every input
outputs = ["${int}/cooked/**"]     #   is as it was when its last good run began, and its command is unchanged
configs = ["Development", "Dist"]  # run only in these configurations
```

Workspace-level steps go in `[[workspace.steps]]` with the same fields. A step that exits non-zero (or a
Python step that returns non-zero or raises) stops the invocation. The folders of a step's `outputs` (the part
before a glob) are made before it runs, so a pre-build step can write into `${int}` of a project never built.

## 5. Commands of your own

```toml
[commands.kit]
description = "Package a shippable folder"
requires = ["build"]               # commands run first, on the same projects
projects = ["Game"]                # optional: always these projects (default: the command line's selection)
steps = [                          # workspace-level steps of the command (phase = the command's name)
	{ name = "zip", run = ["powershell", "-NoProfile", "-Command", "Compress-Archive -Force ${bin.root}/Game ${build.dir}/Game.zip"] },
]
```

Projects attach work to it with `phase = "kit"` (or `pre-kit`, `post-kit`). `nuke kit dist` builds Game in
Dist, runs every project's kit steps, then the workspace's. A workspace command cannot redefine a built-in
one; attach steps to the built-in's phases instead. Command names are lowercase letters, digits and `-`.

## 6. Python extensions

A script listed in `scripts` (workspace or project) defines `register(nuke)`:

```python
def register(nuke):
	@nuke.command("deploy", description="Copy the Dist build to the share", requires=["build"])
	def deploy(ctx):
		for project in ctx.targets:
			ctx.run(["robocopy", ctx.bin_dir(project), r"\\share\builds\%s" % project.name, "/MIR"], check=False)

	@nuke.step("post-build", project="Game", configs=["Dist"])
	def stamp_version(ctx):
		with open(ctx.expand("${bin}/version.txt"), "w") as f:
			f.write(ctx.expand("${project.name} ${config}"))
```

`nuke.command(name, function, description, requires, projects)` and `nuke.step(phase, function, project,
name, configs, inputs, outputs, description)` work as decorators or plain calls; a project's script defaults
`project` to that project. Functions receive a context: `workspace`, `project` (None for workspace steps),
`config`, `configuration`, `arch`, `system`, `args`, `targets`, `log`, `variables`, `expand(text)`,
`run(argv, cwd=None, env=None, check=True)`, `toolchain()`, `bin_dir(project)`, `int_dir(project)`. Return
None or 0 for success.

## 7. Variables

Manifest strings expand `${name}`; an unknown name is an error. `$${` is a literal `${`.

| Variable | Value |
|---|---|
| `workspace.dir`, `workspace.name` | the workspace |
| `project.dir`, `project.name` | the step's project |
| `config`, `arch`, `system` | `Development`, `x64`, `windows` |
| `bin`, `int` | the project's `build/bin/<Config>-<system>-<arch>/<Project>` and `build/int/...` |
| `bin.root`, `int.root`, `build.dir` | the configuration's folders and `<workspace>/build` |
| `output` | the project's program (C4 executable, or a custom project's `output`) |
| `python` | the Python running Nuke |
| `nuke`, `nuke.home` | Nuke's package folder (`${python} ${nuke} ...` runs Nuke) and the repository holding it |
| `toolchains`, `toolchain.dir`, `toolchain.version`, `c4` | the store, the workspace's toolchain and its `C4.exe` |
| `env:NAME` | an environment variable (an error when unset) |
| `args` | the words after `--`; only as a whole argument, where it splices them in |

## 8. Toolchains

A **toolchain** is a complete C4 install - the compiler, its default library and runtime, clang and lld-link,
CBind, C4Script, C4Api (the compiler's C API for tools, section 9c), the language server and debug adapter, the user docs
and the IED plugin of that language version (IED's `plugins/c4`, with its AI skills and a manifest naming that
toolchain's tools) - with a `toolchain.json` (version, git commit, whether the tree was modified, build time). They live
in one store per machine, `<this repository>/toolchains/c4/<version>/` (`NUKE_TOOLCHAINS` overrides the
location), and every workspace shares them:

| `toolchain =` | Means |
|---|---|
| `"0.1.0"` | that installed version; installed versions never change |
| `"dev"` | the compiler built from `C4/` in this repository by its latest Development build |
| `"latest"` or unset | the newest numbered version (unset prints a warning: pin it) |
| `"./tools/c4"` | a folder holding a toolchain (vendored with the workspace) |

```text
nuke toolchain list                        installed toolchains; * marks the workspace's
nuke toolchain which [VERSION]             the folder a version resolves to
nuke toolchain install SOURCE [--as V] [--force]
                                           copy an install (a folder, or "dev") into the store under its version
nuke toolchain remove VERSION
nuke toolchain use VERSION                 rewrite [workspace].toolchain
```

Making one: in this repository `nuke build` refreshes `dev`; `nuke install-c4` freezes the current compiler as
the version in `C4/C4Lib/src/Compiler/Version.h` (bump it first: an installed version is never overwritten
without `--force`). `C4/MakeToolchain.py --out DIR` assembles an install from a compiler build anywhere.

**When the compiler is wrong.** Fix it here rather than working around it: record it in
`C4/docs/status/known-bugs.md`, fix it in `C4/` with a test, `nuke build` and `nuke gates` here, then carry on
- a workspace on `dev` uses the fix at once; a pinned one moves with `nuke install-c4` + `nuke toolchain use`.

## 9. C4 projects: compiling, tests, the IDE bridge

For a program `P`, Nuke writes the build description `build/int/<cfg>/P/projects.toml` (P and every C4 module
it depends on: sources, includes, defines with `<CONFIG>` = 1 and `PROJECT_NAME`, depends; see
`C4/docs/language/15-projects.md`) and runs, from P's folder:

```text
C4.exe --projects <int>/projects.toml -O<n> [-g] [-D NAME=VALUE (configuration defines)] [-l LIB ...] [compiler-args]
       --int-dir build/int/<cfg>/P -o build/bin/<cfg>/P/P.exe
```

It is rebuilt when the output is missing, the command line or description changed, or a tracked file - every file
under the modules' sources and includes, and the toolchain's `C4.exe`, `Bedrock/` and `C4Runtime/` - was added,
removed or changed since the last successful compile began. Each file's state (mtime and size) is taken before the
compiler runs and stamped only after it succeeds, so a source edited while a build ran (another session's build
finishing later included) is compiled by the next build, never reported up to date. Compiler output streams as it happens; the compiler's progress lines (`ENGINE: Phase 1: Parsing`) show only with `-v`, its errors and warnings always. An output that is running (a server under test, the IDE
built from this checkout) cannot be written on Windows, so it is renamed to `<output>.<ms>.old` first and the old
program keeps running; a later build removes the `.old` copies nothing runs any more.

**Tests.** `nuke test` compiles each test-bearing C4 project separately with `-test -test-json
<int>/tests/results.json` into `<bin>/<P>-tests.exe` (a plain build never includes the harness), runs it from
the project folder (with `--filter`), runs every harness even when one fails, prints a summary and exits 1
if any failed. Custom projects test with `phase = "test"` steps.

**IDE bridge.** After each compile of a program (an executable's build, a module's test build), also one that
fails, Nuke writes `<workspace>/.ied/local/lsp-options/<P>.json`: `includePaths`, `defines`, `c4Home` (the
toolchain), `toolchain`, `projects` (the description) and `target` (the `-m...` compiler arguments) - the exact
input the compiler saw, so the language server in IED agrees with the build. IED hands every one of them to one
C4LSP for the workspace, which analyzes a module's files in a program that compiles them. `ide = false` in `[workspace]` turns it off. `.ied/local/` is machine-local; `nuke init` adds it to
`.gitignore`.

**IED solution** (`nuke init --ied`, `nuke/ied_files.py`; IED `docs/03`). Writes `<Workspace>.iedsln` at the root
and a project file per project under `build/ied/` (`root` the project's folder, `kind` executable / library /
other, `buildSystem: {id: "nuke", projectName}`, `references` from `depends`, `language` (the project's; IED picks its icon from it), `presets: ["<language>.nuke"]` - IED's
`nuke` preset, its toolbar and Build / Run / Test tasks running `nuke` (the `nuke.command` setting) with the selected
configuration, extended by the language's plugin with its problem matchers and launch configuration; `["nuke"]` for a
project without a language), and pins the toolchain's IED plugin
(`<toolchain>/ied`, which `C4/MakeToolchain.py` ships) in the solution's `localPlugins` - relative when the toolchain
store is inside the workspace, as it is by default. The IED plugins the workspace develops (`ied-plugins`: folders or
globs of folders with an `ied-plugin.json`) get project files of kind `plugin` - no build system: IED loads them - named
by their ids (`<id> plugin` where a project has that name) in the solution's "IED Plugins" folder; this repository
lists `plugins/*`. Every later Nuke command on the workspace (`nuke build`, `nuke test`, ...) refreshes them when
the solution's `generator` is `nuke init --ied` and the toolchain resolves - a pull that adds a project needs no
`nuke init --ied` - and says so only when a file changed: files it wrote
are rewritten, files of projects gone are removed, and a file it did not write, or one edited since, is left as it
is with a warning (`.ied/local/generated.json` keeps the digest of each file it wrote). The files it wrote under the
names before (`<Workspace>.ied.sln`, `build/ied/*.ied.proj`) go once the new ones are written. The projects' `group`s
become the solution's `groups`, the Solution Explorer's folders (sorted by name, `"Engine/Tools"` nested); a language
package's library outside the workspace builds with it but has no project file (the explorer lists only the
workspace's files). It also adds `.ied/local/` to
`.gitignore`, as `nuke init` does for a new workspace. The AI skills are not copied into the workspace: C4's come with the
toolchain's IED plugin, Nuke's with IED's `ied.nuke` plugin (IED docs/26).

## 9b. Projects in other languages

A project whose `language` is not C4 builds, tests and runs through its language's **driver** (IED `docs/26`
section 6): Python in the language's package - an IED plugin folder with `nuke/nuke-language.toml` - that runs the
language's own tool and tells Nuke what to copy into `build/bin`. Native first: the tool's manifests (`Cargo.toml`,
`go.mod`), lockfiles and caches (`target/`, Go's build cache) stay its own, building in IED and outside it share them,
and Nuke adds only what is cross-language: one command, the configuration names, the order `depends` gives across
languages, and the outputs in one place.

| Language | Package | Builds | Configurations |
|---|---|---|---|
| `rust` | IED's `plugins/rust` | `cargo build --message-format=json-render-diagnostics` (diagnostics to the log, artifacts read from cargo's messages); executables and dynamic/static libraries with their `.pdb` are copied | no optimization: the dev profile; otherwise `--release`, with debug info (`CARGO_PROFILE_RELEASE_DEBUG`) when the configuration keeps it. `[project.driver]`: `package`, `bin` |
| `go` | IED's `plugins/go` | `go build -o build/bin/.../<Name>.exe <package>` (a library: `go build ./...` to check it); `go test ./...` | no optimization: `-gcflags=all=-N -l`; no debug info: `-ldflags=-s -w`; GOOS/GOARCH from the platform. `[project.driver]`: `package` (default `.`), `name` |
| `cpp` | IED's `plugins/cpp` | the driver is the build (section 9c): clang `-cc1` per source, lld / llvm-lib / `-shared` link into `build/bin`, the Reflection library's code generation | the configuration's optimization and debug info, its name defined. `[project.driver]`: `library`, `reflection`, `system-includes`, `linker-args` |

A language's package is the one the workspace pins in `[languages]` (a folder, relative to the workspace), else one
of the workspace's own plugins (`.ied/plugins/<id>/`: a project that invents a language ships its driver with it),
else one of IED's plugins beside Nuke (`NUKE_LANGUAGE_PACKAGES` adds folders to look in). A package may have no
driver (no `module` in its description): it names its projects' preset and brings library projects, and a project in
its language is refused. C4's is one (`plugins/c4/nuke`): Nuke compiles C4 itself (IED docs/23 N-23), and the package
brings `C4Api`, the compiler's C API from the workspace's toolchain (section 9c). `NUKE_TOOL_<NAME>`
(`NUKE_TOOL_CARGO`) names the tool when it is not on `PATH`.

```toml
# Tools/nuke.toml
[project]
language = "rust"                  # its Cargo.toml sits beside this file
depends = ["Engine"]               # built after Engine, whatever its language
```

A driver (`nuke/nuke-language.toml` names it and the IED preset of its projects) is a module - or a Python package,
when the module named is an `__init__.py` - with `build(ctx) -> [artifacts]`, `program(ctx) -> file name or None`,
`test(ctx) -> exit code`, and optionally `clean(ctx)`, `has_tests(ctx)`, `implicit_depends(spec) -> [names]` (projects
a project needs besides its `depends`, added to them: C++ reflection needs the Reflection library and its tools),
`variables(ctx) -> {name: value}` (what manifests and steps read as `${<language>.<name>}`: `${cpp.llvm}`) and
`editor(folder, contexts) -> {file name: text or None}` (while a package source is developed, files in `build/develop/`
for the language's server in IED, from the contexts of all the workspace's projects in the language: section 12); `ctx`
(`nuke/languages.py`) carries the project, the configuration, the platform, the bin and int folders, the extra
arguments and test filter, `options` (`[project.driver]`), `settings` (the workspace's `[languages.<id>]` table),
`depends` (the contexts of the projects in `depends`), `expand(text)` (the project's variables), `jobs` (`-j`),
`checkouts` (the package sources the workspace develops: `source`, `url`, `folder`, `serves(url)`, `files(name)`) and
`develop_file(name, text)` (a file in `build/develop/<project>/` that points the tool at their clones, section 12),
`tool(name)`, `run`/`capture` to run the tool with its output in Nuke's log, and `problem(message)` / `fail(what, code)`
to stop Nuke (a ValueError from `implicit_depends` or `variables` is a problem too). The description may also name
the package's **library projects** (`libraries = ["libraries/*"]`, folders with a `[project]` nuke.toml): a workspace
depending on one by name gets it as a project, built into its own `build/`; and ask for `build-before-test = true`
(`nuke test` builds such a project, with what it depends on, before `test(ctx)`). `nuke init --ied` gives such a project its package's preset
(`rust.nuke`, `go.nuke`), the program in `buildSystem.output` (IED's `${projectOutput}`), and pins a `[languages]`
package that is an IED plugin in the solution's `localPlugins`.

## 9c. C and C++ projects

`language = "cpp"` builds with IED's C/C++ plugin (`plugins/cpp/nuke/driver/`), the C++ build system Frost was, as a
language driver: an LLVM release's clang compiles each source with its `-cc1` frontend for the MSVC ABI (with Visual
Studio's headers and the Windows SDK's), and the objects link into an executable (lld), a static library (llvm-lib) or
a DLL (clang `-shared`) in the project's bin folder.

```toml
# Engine/nuke.toml
[project]
kind = "library"                   # a static library (.lib); kind = "executable" for a program
language = "cpp"
depends = ["Core"]                 # its include folders and library reach this project and what links it
sources = ["src", "third_party/lua/*.c"]   # folders (every .c and .cpp below), files or globs; default ["src"]
includes = ["include"]             # include folders it gives itself and its dependents, after its source folders
defines = { ENGINE = "", LEVEL = "2" }     # an empty value defines the name alone
links = ["user32.lib", "${cpp.llvm}/lib/LLVM*.lib", "!LLVM-C.lib"]   # names, paths or globs; "!NAME" leaves a file out
compiler-args = ["-Wno-unused-variable"]   # clang -cc1 arguments, after the driver's own
tests = true                       # (an executable) a test program: `nuke test` runs it with -filter=GLOB

[project.driver]
library = "dynamic"                # a DLL (and its import library) instead of a static library; "prebuilt": below
reflection = true                  # the Reflection library's HeaderTool and CodeGen run over the sources
system-includes = ["sdk/include"]  # this project's own system include folders
linker-args = ["-fuse-ld=lld"]
```

```toml
# the workspace's nuke.toml
[languages.cpp]
llvm = "vendor/llvm"               # the LLVM release (default: the folder above the clang on PATH, else the IED
                                   # checkout's vendor/llvm, which setup.bat installs)
system-includes = ["${cpp.llvm}/include"]   # system include folders of every C++ compile
standard = "c++20"                 # -std= of .cpp files; c-standard = "c11" for .c files
```

- **Includes and links travel through static libraries.** A project compiles with its own include folders and those
  of every C++ static library it reaches through static libraries; a program or DLL links its objects with those
  libraries, their `links` and its own (less what its `"!NAME"` entries leave out). A static library holds its own
  objects only. A program or DLL in `depends` only orders the build: a tool it runs, a program shipped beside it.
- **Compiles.** A source compiles when it has no object or dependency file yet, its arguments changed, clang is newer,
  or a file it read changed or went (clang's dependency files). An object is stamped with the time its compile began,
  so a file edited during the build compiles again the next time. `-j N` (`NUKE_JOBS`) compiles N at once, by default
  one per processor. A crash of clang or the HeaderTool - they crash now and then under a parallel build on Windows,
  never on a rerun - is retried twice; an error never is. A failed compile leaves no object or dependency file, and
  nothing links. The objects of removed sources go; two sources that would share an object (`a/Types.cpp`,
  `b/Types.cpp`) are an error.
- **Configurations.** The optimization is the configuration's (Debug `-O0`, Development and Dist `-O3`), with CodeView
  debug information and a `.pdb` when it keeps debug info. Every compile defines the configuration's name in capitals
  (`DEVELOPMENT`), `UNICODE`, `_UNICODE`, `_CRT_SECURE_NO_WARNINGS`, `PROJECT_NAME=<name>` and the configuration's
  `defines`. Diagnostics are in MSVC's form, `file(line,col): error: ...` (the C/C++ plugin's `clang-msvc` matcher), at
  most 10 errors a file.
- **Links** run when the output is missing, an object or library is newer, or the arguments changed, through a
  response file (a deep folder's objects outgrow a command line). A running program or DLL is moved aside first
  (`<file>.<ms>.old`), as Nuke does for C4 programs.
- **Copies.** A program or DLL gets its `[[project.copy]]` entries and those of the libraries it links: a file when it
  changed, a folder mirrored (but what its `exclude` leaves out, which the mirror removes). A program whose project has
  an `assets/` folder gets `<bin>/assets` linked to it.
- **Reflection** (`reflection = true`): the project depends on the package's `Reflection`, `HeaderTool` and `CodeGen`
  (added to its `depends`). Each source goes through the HeaderTool after it compiles; CodeGen gathers what it found
  into `<int>/generated/generated.cpp`, compiled and linked with the project (`plugins/cpp/docs/ReflectionSystem.md`).
- **clangd.** Every build merges its sources' compiles, in the clang driver's form, into `build/compile_commands.json`,
  which IED's clangd finds for every file of the workspace.
- **Prebuilt libraries** (`library = "prebuilt"`, `sources = []`): an SDK's files, nothing built. Its `includes`,
  its `links` (library files) and its `[[project.copy]]` reach what depends on it as a static library's do, and each
  must be there (the build stops naming the ones that are not). The c4 package's `C4Api` is one:
  `includes = ["${toolchain.dir}/C4Api"]`, `links = ["${toolchain.dir}/C4Api/C4Api.lib"]` and `C4Api.dll` copied beside
  the program, so `depends = ["C4Api"]` links a tool with the compiler's C API of the workspace's toolchain (C4
  `docs/tools/06-c4-api.md`; the sample `plugins/c4/samples/c4-outline`).
- **Variables.** `${cpp.llvm}` and `${cpp.clang}` name the LLVM release and its `clang.exe`, in manifests and steps.
- **Libraries** the package brings (`plugins/cpp/libraries/`, documented in `plugins/cpp/docs/`): `Core` (containers,
  strings, UUID, `Ref<T>`, logging, time, the Math library) with yaml-cpp as `yaml`, `Threading`, `Profiling` (the IED
  Trace Protocol client), `Reflection`, `StaticReflection`, `HeaderTool`, `CodeGen`, `ObjectDescription`,
  `ChangeNotification`, `UnitTesting` (the test and benchmark harness: a test program takes `-filter=GLOB`,
  `-benchmark-filter=GLOB`, `-list-tests=1`, `-list-benchmarks=1`) and their tests, `CoreTesting` and `ReflectionTesting`.
  A workspace depending on one by name gets it; this repository lists them all (`plugins/cpp/libraries/*`).

What Frost did that the driver does not: Visual Studio solution files (IED is the IDE: `nuke init --ied`), its
AddressSanitizer build (`-asan`), and the per-file compile-time table (`-ci`; `build/int/<cfg>/nuke-trace.json` has
each project's time).

## 10. This repository as a workspace

The repository root's `nuke.toml` makes IED itself a workspace. The C4 compiler and its tools are C++ projects in the
`C4` group, built by the C/C++ plugin's driver (section 9c) with the LLVM release `setup.bat` installs
(`[languages.cpp] llvm = "vendor/llvm"`): `C4` (`C4/nuke.toml`: C4.exe and the install layout beside it), `C4Lib`,
`C4Protocol`, `C4LSPLib`, `C4LSP`, `C4DAPLib`, `C4DAP`, `CBind`, `C4Script` and `C4Api` (DLLs) and `C4Testing`, each in its own
folder under `C4/`; the C++ libraries they use are the plugin's (`plugins/cpp/libraries/*`). Outputs are Nuke's:
`build/bin/<Config>-windows-x64/C4/C4.exe`, `.../C4LSP/C4LSP.exe` and so on.

| Command (from the root) | Does |
|---|---|
| `nuke build` / `nuke build debug` | C4.exe and the tools the toolchain ships (C4LSP, C4DAP, CBind, C4Script); a Development build refreshes the `dev` toolchain (`C4/MakeToolchain.py --bin ${bin.root}`) |
| `nuke test` | every project's tests: C4Testing, CoreTesting, ReflectionTesting, the server's and the CLI's (`nuke test C4Testing --filter "Lexer.*"`) |
| `nuke gates` | build, then every C4 suite: unit, e2e, negative and isolated (-O0 and -O2), runtime, script (-O0 and -O2), tools, Bedrock |
| `nuke install-c4` | build, then install the compiler as a numbered toolchain |
| `nuke clean` | the configuration's outputs |

IED's own server (`src/`, `ied-server`), the CLI, the launcher and the desktop app are C4 projects of the same
workspace, built with `toolchain = "dev"`.

## 11. Layout of Nuke itself

```text
Nuke/
  nuke/               the package (python Nuke/nuke ... runs __main__.py)
    cli.py            words -> commands, configuration, projects; exit codes
    manifest.py       nuke.toml parsing and validation
    workspace.py      finding the workspace, discovery, extension scripts
    graph.py          dependency validation, cycles, build order
    engine.py         phases: requires, pre-/post- steps, built-in work, once per project
    steps.py          running steps; incremental inputs/outputs; the Python step context
    session.py        one invocation: configuration, layout, variables, the toolchain
    toolchains.py     the store: resolve, install, remove, pin
    variables.py      ${name} expansion
    configs.py        built-in and workspace configurations
    languages.py      language packages and drivers: finding them, loading them, their context (section 9b)
    packages/         packages from git (section 12): versions, the resolver, the lock, installs, IED's view
    c4/               compiling C4 programs and harnesses, the build description, the IDE bridge
    commands/         one module per built-in command
  templates/          what `nuke init` writes
  tests/              python -m unittest discover -s Nuke/tests (unit tests use a stub compiler;
                      test_integration.py uses a real installed toolchain and skips without one;
                      test_packages.py makes real git repositories in a temporary folder)
```

## 12. Packages

A **package** is a folder of a git repository whose `nuke.toml` has a `[package]` table with its `version`; its
projects (the folder's own `[project]` and every project folder below it that is not inside another package) join any
workspace that uses it. One repository holds as many packages as you like, each versioned on its own, so common code
lives in one private repository and every program takes the parts it needs. Design and reasons: IED `docs/31`.

A project - or a package - lists what it uses in its own `nuke.toml`, one form for everything:

```toml
# Game/nuke.toml
[project]
name = "Game"
kind = "executable"

[dependencies]
# a package from git: the repository's URL followed by the package's folder there, and the versions it accepts
RhiD3D12  = { source = "https://github.com/AlanBuechner/Claymore.git/Renderer/RHI/DX12", version = "^0.2.1" }
Windowing = { source = "https://github.com/AlanBuechner/Claymore.git/Windowing", version = "~0.2.1", writable = true }
Utils     = { source = "git@github.com:me/utils.git/Utils", branch = "main" }    # a branch (the lock pins its commit)
Strings   = { source = "git@github.com:me/utils.git/Strings", rev = "1a2b3c4d" } # one commit
# a local folder, written from the workspace's root: another project of the same package (or, for a project in no
# package, of the workspace)
Core      = { source = "Engine/Core" }
```

`nuke.toml` is the only file you write. Any Nuke command (`nuke build`, `nuke init --ied`, ...) fetches what is missing,
installs it under `packages/` and writes the lock there, `packages/nuke.lock` - generated, and ignored by git with the
rest of the folder - and follows a changed `nuke.toml` the next time. A dependency's projects are linked to the project
by themselves (no `depends` entry); the entry's name is yours (messages and `nuke package remove` use it).

- **A source** is the repository's URL with the package's folder after it: after `.git/` when the URL has it
  (`https://host/o/r.git/A/B`, `git@host:o/r.git/A`, a local repository `C:/repos/r.git/A`), else after a host's
  owner and repository (`https://github.com/o/r/A/B`); the repository itself for a package at its root. Anything else
  is a local folder, written from the workspace's root (`Engine/Core`; for a package's project, from its repository's
  root - its workspace's - wherever it is installed). `../` and absolute paths are refused.
- **A local dependency stays in its package.** A package's project may name only projects of the same package by
  folder - Nuke refuses one that reaches outside it - so whoever uses a package gets everything it uses. Another
  package, of this repository or another, is a source with its URL and a version.
- **A repository builds its own packages from its folders**: a dependency whose URL is one of the checkout's remotes
  and whose folder is a package of the workspace is not fetched (Claymore's own workspace builds every package together).

**Locked or writable.** A package is installed **locked** - read-only on disk, at the lock's commit - unless its
dependency says `writable = true`: then the build clones its repository into `packages/<repository>/` (where the clone
is missing: on another checkout of the workspace too) and every package of that repository builds from the clone, so
you edit it together with the program (section "Developing a package" below). Taking `writable` away goes back to
locked installs at the next build - refused while the clone has uncommitted changes or commits no remote has, so no
work is lost. `nuke package develop` and `nuke package lock` do the same for one machine without changing `nuke.toml`.

```toml
# Windowing/Win32/nuke.toml in the claymore repository: a package of its own inside Windowing's folder
[package]
version = "0.2.1"                           # nuke package release writes it and tags Windowing/Win32/v0.2.1
description = "The Win32 backend of Windowing"

[project]
name = "WindowingWin32"
kind = "module"

[dependencies]                              # every package it uses, its own repository's too
Windowing = { source = "https://github.com/AlanBuechner/Claymore.git/Windowing", version = "^0.2.1" }
Agility   = { source = "https://github.com/me/Agility.git/SDK", version = "^1.6" }
```

- **Versions** are a package's `[package] version` and git tags `<package folder>/v<semver>` (`Windowing/v0.2.1`,
  `Renderer/RHI/v1.0.0`), or `v<semver>` for the whole repository - Go's tags for modules in subfolders. A package
  without tags of its own is released with the nearest package above it that has some.
- **Requirements** are Cargo's: `^1.2` (same major; same minor below 1.0), `~1.2.3` (same minor), `=1.2.3`, `>=1.2, <1.5`,
  `*`. A bare `1.2` is `^1.2`. `*` on a package that was never released takes the default branch's tip.
- **Dependencies are written, never inferred**: each package's `[dependencies]` are followed from its manifest at the
  version chosen; a package's project that depends on another package's project without naming that package is an
  error that says what to write.
- **Resolution** takes the highest version every requirement accepts, keeps a locked version while it still does, and
  when requirements on a package exclude each other tries older versions of the packages that made them before it
  stops with every requirement and who made it.
- **`packages/nuke.lock`** holds the commit of each versioned package and the packages of it used; Nuke writes it
  from the manifests (generated, not committed: each checkout chooses its versions on its first command, and keeps them
  until `nuke package update` or a changed requirement). While it matches the manifests and everything is installed,
  Nuke reads only files: no git process, no network. A `nuke.lock` beside `nuke.toml` (older Nukes) moves there.
- **Installs** go into the workspace's `packages/` folder (`[workspace] packages-dir`): a locked package is
  `packages/<source>/<path>@<version>/<folder>/` with every file read-only. Nuke keeps the folder out of git through the
  checkout's `.git/info/exclude`, not `.gitignore`, so search in IED and the AI's tools - which skip what `.gitignore`
  ignores - find the packages' code. Each repository is mirrored once per machine in `<nuke home>/.cache/packages/git/`
  (`NUKE_PACKAGE_CACHE`).
- **Private repositories** need nothing of Nuke: git's credential manager or SSH key does it. Git never prompts under
  Nuke - not on the terminal, not in the credential manager's window (IED runs `nuke package status --fetch` on a
  timer, IED `docs/31` section 6); a missing sign-in is an error saying to run `git ls-remote <url>` once.
- **A repository of packages is a workspace** too: its root `nuke.toml` has `[workspace]`, it builds and tests every
  package together from its folders, and what its packages need from other repositories is fetched.

```text
nuke package [status] [--fetch] [--json]   the versioned packages: version, commit, locked or developed, projects;
                                           --fetch: newer releases on the remotes, how far each checkout is behind
nuke package update [NAME...]              the newest versions the requirements allow (all, or repositories / packages)
nuke package add SOURCE [REQ]              add a dependency to a project's nuke.toml: a package from git ("<URL>/<folder>";
            [--project NAME] [--name N]    without REQ, ^ its newest release) or a local folder; the project is
            [--branch B | --rev C]         --project, else the one the current folder is in, else the default
            [--writable]
nuke package remove NAME [--project NAME]  take a dependency out (by its name, source or folder)
nuke package develop REPOSITORY [--at DIR] build a repository's packages from a git clone in packages/<name>/ (or DIR)
nuke package lock REPOSITORY [--keep]      back to locked installs (refused while the clone has unpushed work; the
                                           clone stays when the lock cannot be installed; --keep moves it aside)
nuke package pull REPOSITORY               bring a develop clone up to date (git pull --rebase)
nuke package release PACKAGE... [--major | --minor | --patch | --version X.Y.Z] [--source NAME] [--no-push]
nuke package browse REPOSITORY [--offline] [--json]   the packages at a repository's default branch (its URL, or the
                                           name of one the workspace uses), each with its versions and what the
                                           workspace requires and has of it; --offline: no fetch
```

`status --json` is what IED's Package Manager reads: `sources` (the repositories the workspace's projects name) and
`requirements` (each dependency from git a project declares: name, source, url, path, requirement, project, file), `packages` (each
versioned package of the lock: `source`, `url`, `path`, `version`, `commit`, `uses`, `projects`, `folder`, `mode`,
`writable`, `links` to its pages on the source's host, `versions` newest first, `newest` and `compatible` - the newest
release, and the newest `update` reaches within the locked version's major - when newer than the lock's, `packages` with
descriptions, `dependsOn` and `requiredBy`: the dependency graph with each requirement), `checkouts` (each develop clone)
and, with `--fetch`, `unfetched` (clones that could not be fetched; exit status 1).

**Developing a package with the program that uses it.** `nuke package develop claymore` clones the repository into
`packages/claymore/` at the tip of its default branch; from then on every claymore package builds from that working
tree, so the program and the packages are edited, built, run and debugged as one codebase (`writable = true` on a
dependency does it for every checkout of the program). Every Nuke command writes the clone's HEAD into the lock - its
release (`0.2.0`) or a pseudo-version after it (`0.2.0+3.1a2b3c4d`, three commits later). `nuke package release
Windowing --minor` writes `version = "0.3.0"` into `Windowing/nuke.toml`, commits, tags `Windowing/v0.3.0` and pushes
the branch and the tag (a version written by hand is released as it is); a package of the same repository whose
`[dependencies]` entry no longer accepts the new version gets `^0.3.0` there and is released again with it. It refuses
uncommitted changes, a clone behind its remote, and a package of the same repository it uses with unreleased changes
(it says to release that one too), and moves the program's own requirement when the new version is outside it.
`nuke package lock claymore` goes back to read-only installs at the lock's commits.

**One writable copy.** A locked package's files are read-only. A develop clone that is behind its remote - another
checkout pushed changes since - is read-only to IED until `nuke package pull` brings it up to date, so every change
starts from the newest code. Nuke writes what IED needs on every command: `.ied/local/packages.json` (every package,
its folder, version, mode and projects; every clone with its branch and how far it is ahead and behind) and
`.ied/local/readonly/nuke.json` (the folders IED opens read-only, each with the reason). `nuke init --ied` (and every
package command) puts the packages' projects in the solution's `Packages/<source>/<folder>` folders, their project
files naming the package's version and mode (`buildSystem.package`) and the packages each project uses (`packages`,
its Dependencies node); a package manifest above its projects is an item of the solution folder it belongs to. IED's
Package Manager (IED `docs/31` section 8) runs these commands for you.

**Other package managers.** A Rust or Go project keeps its own manifest, lockfile and cache; Nuke never resolves
crates or modules and never writes `go.mod`, `go.sum`, `Cargo.toml` or `Cargo.lock`. The tags are Go's, so a Go module
in a package folder has the same versions for `go get`, and Cargo's git dependencies take the same tag
(`tag = "Mathx/v0.2.0"`). While a source is developed, the workspace's Go and Rust projects build its clone too, through
their own tools, so an edit there reaches the Go and Rust programs without a release:

- **Go**: a project whose `go.mod` requires a module of the clone (matched by the module paths of the clone's `go.mod`
  files, and followed through what those require) builds with `GOWORK` naming `build/develop/<project>/go.work`, which
  uses the project, those module folders and whatever the project's own `go.work` uses.
- **Rust**: a project that gets crates from the source's repository (a git dependency of its manifests or a git package
  of its `Cargo.lock`, the URL in any spelling of the repository) builds with `cargo --config
  build/develop/<project>/cargo.toml`: `[patch."<url>"]` points those crates at their folders in the clone, and
  `resolver.lockfile-path` keeps the resolution in a `Cargo.lock` beside it, copied from the project's (cargo 1.98 has
  that setting; an older cargo leaves the patched resolution in the project's `Cargo.lock` while developing).

- **IED's language servers**: on every build, test or package command while a source is developed, the drivers'
  `editor()` write workspace-wide files into `build/develop/` (`nuke/develop_files.py`): `go.work` (every Go project
  and the clone modules they require) with `gopls.json` naming it, and `cargo.toml` (the `[patch]` tables of every Rust
  project) with `rust-analyzer.json` naming it and giving each Cargo workspace a lockfile of its own in
  `target/nuke-develop/`. The go and rust plugins' gopls and rust-analyzer read them, and IED starts them again when
  the files change, so go-to-definition lands in `packages/<source>/` rather than in the release.

`nuke package lock` removes `build/develop/` first (IED's servers then let go of the clone), waits a while for any
program still holding a file of the clone, and the tools go back to the releases. `go` or `cargo` run by hand in a
terminal still see the releases: the files are handed to them by Nuke and IED only.
