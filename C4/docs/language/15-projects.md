# 15 - Projects: several projects in one build

A C4 program is compiled in one piece, so a game and the engine it uses build together and every call can be
inlined across them. A **project** keeps its own settings and its own boundary inside that one build: its
defines, its include roots, and the rule that a project uses only the projects it depends on.

**Status:** **Implemented** (B38, D-59). `C4.exe --projects <file>` builds a described set of projects; Nuke writes the
description for every build, and the language server reads the same file.

---

## 15.1 What a project is

| Part | Meaning |
|---|---|
| name | unique in the build; diagnostics and `PROJECT_NAME` use it |
| sources | the folders (or files) whose `.cf` / `.ch` files are the project's |
| includes | further folders its `include` lines search (C4 headers and `extern "c"` headers) |
| defines | macros for the project's files, on top of the predefined ones and the command line's `-D` |
| depends | the projects it uses |

One project is the **root**: the program. The build is the root and every project it depends on, directly or
through another. Bedrock is not a project of the build: every project may include it, and the prelude is in
every file ([14](14-builtins.md)).

## 15.2 Describing a build

The build tool writes one description of the whole build, a small TOML file, and hands it to the compiler:

```bat
C4.exe --projects build\int\Development-windows-x64\Game\projects.toml -o build\bin\...\Game.exe
```

```toml
root = "Game"

[[project]]
name = "Game"
sources = ["C:/Work/Game/src"]
depends = ["Engine"]

[project.defines]
PROJECT_NAME = "Game"

[[project]]
name = "Engine"
sources = ["C:/Work/Engine/src"]
includes = ["C:/Work/Engine/third_party"]
depends = []

[project.defines]
PROJECT_NAME = "Engine"
ENGINE_EDITOR = "1"
```

- The file uses a subset of TOML: `#` comments, `key = "string"`, `key = ["string", ...]` (over several lines
  too), `[[project]]` and `[project.defines]` tables. Anything else is an error with its line.
- Paths are absolute, or relative to the description's folder.
- Nuke writes it into the program's intermediate folder for every compile, from each project's `nuke.toml`
  (`sources`, `includes`, `defines`, `depends`), and names the same file in the language server's options, so
  the editor sees the build the compiler sees ([../tools/02-nuke.md](../tools/02-nuke.md#2-how-a-program-is-compiled)).
- A command line without `--projects` builds as it always did: its inputs and every `-I <dir>` are one project,
  and `-D` applies to every file. Boundaries and per-project defines need a description. `--projects` with inputs is
  a usage error (exit 2): the description names every project's sources.
- The command line's `-D` still applies to every project, under each project's own defines.

## 15.3 The rules

1. **Every file belongs to one project**, the one whose source folder holds it. A file in the folders of two
   projects is an error naming both.
2. **A file is preprocessed with its project's defines.** `#if` in an engine file sees the engine's macros, not
   the game's. A header still has one meaning for every includer (rule 1.3.1): its own project's macros decide it.
   An include carries into the includer the macros the included file defines (and those its own includes carry),
   never the defines its project started it with: an engine header does not leak `ENGINE_FAST` or replace the
   game's `PROJECT_NAME`.
3. **An include resolves from the including file's project**, in this order, the first match winning:
   1. relative to the including file's folder;
   2. the project's source folders, then its include folders;
   3. the source and include folders of the projects it depends on, directly or not;
   4. the default library.

   Two dependencies that both have the path, at the same step, are an error naming both files.
4. **A project uses only what it depends on.** Including a file of a project that is not the file's own and not a
   dependency of it is an error: `cannot include 'player.ch': it is a file of project 'Game', and project 'Engine'
   does not depend on 'Game'`. Names follow the same rule: in a body a name is visible only through includes
   ([1.4](01-program-model.md#14-visibility-declarations-vs-bodies)), which the include rule bounds; in a
   declaration (a field's type, a base class, a signature), where names resolve program-wide, a name from a project
   the declaration's project does not depend on is an error: `'Player' is declared in project 'Game', and project
   'Engine' does not depend on 'Game'`. So a reusable project can never call into the project that uses it. A
   template argument is not a name the template wrote: an engine template used with a game type works.
5. **Dependencies form no cycle**; a cycle is an error that prints it (`Game -> Engine -> Game`).
6. **One program.** Code generation sees one module, so inlining crosses projects, and the one-definition rule
   holds across them: two projects defining the same global name is a redefinition, as linking two C++ libraries
   with the same symbol would be.
7. **Tests.** A `-test` build runs the `[test]` functions of every project in the build.

## 15.4 How the compiler does it

| Part | What it does |
|---|---|
| `Compiler/ProjectFile` | reads the description (the TOML subset), with line-numbered errors |
| `Compiler/Projects` | checks it (unique names, known dependencies, no cycle, no folder in two projects, every project used by the root); answers which project owns a file and which projects one may use |
| `SourceManager` | each source records its project (`SetProjects`); `ResolveInclude` follows rule 3 from the including file and reports the ambiguity and boundary errors; there is no basename fallback |
| `Compiler/SourceMacros`, `MacroPass::RunFiles` | each file starts with its project's macros; an include carries the included file's `exported` table |
| `ProgramModel::OutOfProjectReach` | declaration-level lookups (`TypeResolver::ResolveTypeInContext`) reject a name from a project out of reach |
| interop | `InteropFileInput::folders`: an `extern "c"` header is searched in its file's project folders and its dependencies' |
| driver, LSP, Nuke | `--projects`; the LSP's `projects` option; Nuke writes `projects.toml` and the build-derived LSP options file (`.ied/local/lsp-options/<project>.json`) names it |

## 15.5 Tests

- C4Testing `Compiler.Projects`: reading the description, its errors, the checks (a cycle, a folder in two
  projects, an unused project), who may use whom.
- C4Testing `SourceManager.Projects`: owners, the resolution order, the ambiguity and boundary errors, each file's
  defines, what an include carries.
- C4Testing `Lsp.Server`: the editor gets the projects' defines and the boundary error.
- ToolsTest `ProjectDefines` (each project's defines, and at `-O2` the engine function inlined into the game),
  `ProjectBoundaries` (an include and a declaration across the boundary, an ambiguous path, a cycle, a misspelled
  key, inputs with `--projects`), `NukeToolchainProject` (a module's own define reaches only the module).
