# Build System Documentation

Frost, the build system for the C4 compiler and its C++ support libraries, is a Python-based toolchain that orchestrates C++20 compilation using Clang/LLVM 22.1.0 with MSVC-compatible ABI, manages project dependencies, and integrates with Visual Studio via direct Python-generated solution files.

All commands run from the **Frost** directory using `python Build.py`.

---

## Table of Contents

- [Orchestrator (Build.py)](#orchestrator-buildpy)
- [Configuration](#configuration)
- [Project Loading & Dependency Resolution](#project-loading--dependency-resolution)
- [Two-Stage Builds](#two-stage-builds)
- [Compilation Pipeline](#compilation-pipeline)
  - [ObjectBuilder](#objectbuilder)
    - [Stale Intermediate File Cleanup](#stale-intermediate-file-cleanup)
  - [Linker](#linker)
  - [Compile Database](#compile-database)
- [Build Info & Metrics](#build-info--metrics)
  - [Metrics Data Model](#metrics-data-model)
  - [Saving and Loading Build Info](#saving-and-loading-build-info)
  - [Chrome Tracing Export](#chrome-tracing-export)
  - [Compile Time Reporting (-ci)](#compile-time-reporting-ci)
- [Visual Studio Integration](#visual-studio-integration)
  - [VS Generator](#vs-generator)
  - [Backup](#backup)
- [Project Definition Pattern](#project-definition-pattern)

---

## Orchestrator (Build.py)

`Build.py` is the single entry point for all build operations. It performs these phases in order:

### Phase 1 - Toolchain Check

Frost downloads nothing. At import time `Build.py` checks for the LLVM toolchain in `vendor/Compiler/` and, when it
is missing, exits with `error: no LLVM toolchain at <folder>; run setup.bat at the root of the repository`.

`setup.bat` at the root of the IED repository (the folder above the C4 tree) installs the dependencies, pinned
with SHA256 sums in `tools/setup/deps.toml`:

| Dependency | Destination | How |
|------|-------------|--------|
| Clang/LLVM 22.1.0 | `Frost/vendor/Compiler/` | the LLVM GitHub release |
| Python 3.12.10 | `vendor/python/` at the repository root | python.org embeddable package (used by the C4 tree's `Build.bat`) |
| yaml-cpp | `Projects/Core/Modules/yaml/yaml-cpp/` | git submodule (fork AlanBuechner/yaml-cpp) |

### Phase 2 - CLI Argument Parsing

The orchestrator accepts these arguments via `argparse`:

| Argument | Description |
|----------|-------------|
| `-gs` | Generate Visual Studio project files |
| `-b` | Build the specified project only |
| `-fb` | Full build - target project and all dependencies |
| `-rebuild` | Clean then rebuild |
| `-clean` | Remove intermediate and generated files |
| `-r` | Run the built executable after building |
| `-p <path>` | Target project - must be an absolute or relative filesystem path to the project folder (e.g., `C:/Work/C4` or `../C4`). The path is resolved to an absolute path. Short project names are not supported for targeting. |
| `-c <config>` | Configuration: `Debug`, `Development`, `Dist` |
| `-a <arch>` | Architecture: `x64` (alias `x86_64`) |
| `-s <system>` | System: `windows` |
| `-ci` | Print compile time information per file and folder after build |
| `-test` | Run unit tests for the specified project (use with `-p`) |
| `-benchmark` | Run benchmarks for the specified project (use with `-p`) |
| `-testall` | Discover and run all unit test projects |

### Phase 3 - Project Script Loading

The orchestrator passes a list of project paths to `ProjectManager.LoadProjects()`. This list includes built-in core projects (Core, Reflection, CodeGen, etc.) plus the user-specified target from `-p`.

After loading, every project's `SetupProject()` hook runs, which triggers each project's optional `DownloadDependencies()` lifecycle callback.

### Phase 4 - Action Execution

Depending on flags, one or more of these actions occur:

1. **Generate** (`-gs`): Calls `VSGenerator.GenerateAllVSFiles()` to emit `.sln` and `.vcxproj` files directly from Python.
2. **Clean** (`-clean` / `-rebuild`): Removes each project's intermediate directory (`bin-int/...`) and generated folder (`generated/...`).
3. **Build** (`-b` / `-fb` / `-rebuild`): Iterates projects in two stages (EARLY then NORMAL), invoking `ProjectEnvironment.Build()` for each.
4. **Run** (`-r`): Executes the built executable via `subprocess.run()`.
5. **Test** (`-test`, `-benchmark`, `-testall`): Runs unit tests or benchmarks using `Builder.TestRunner`. See [Test Execution](#test-execution) below.

---

## Configuration

Global build settings live in `Config.py`. Key values:

| Setting | Default | Description |
|---------|---------|-------------|
| `configuration` | `"Development"` | Active build configuration |
| `architecture` | `"x64"` | Target architecture |
| `system` | `"windows"` | Target OS |
| `cppVersion` | `"c++20"` | C++ standard |
| `useAsan` | `False` | Enable AddressSanitizer |
| `useDistPDBs` | `False` | Include PDBs in Dist builds |

### Configurations

| Configuration | Optimization | Debug Info | Description |
|---------------|-------------|------------|-------------|
| `Debug` | `-O0` | CodeView standalone, full PDBs | Unoptimized debug builds |
| `Development` | `-O3` | CodeView standalone, full PDBs | Optimized development (default) |
| `Dist` | `-O3` | PDBs only if `useDistPDBs=True` | Distribution/release builds |

### Output Directories

- **Bin**: `bin/{config}-{system}-{arc}/{projName}/`
- **Intermediate**: `bin-int/{config}-{system}-{arc}/{projName}/`

### Tools Directory Configuration

The build system supports a separate output path for tool projects via `Config.tools`. Projects listed in this config use dedicated directories:

| Config | Default | Description |
|--------|---------|-------------|
| `tools` | `["tools/Socrates"]` | List of tool project paths to build separately |
| `toolsBinDir` | `"bin/tools/{projName}"` | Output directory pattern for tool binaries |
| `toolsIntDir` | `"bin-int/tools/{projName}"` | Intermediate output directory pattern for tools |

Helper functions in `Utils/Utils.py` resolve these paths:

| Function | Description |
|----------|-------------|
| `GetToolsBinDir(projName, root=None)` | Returns the formatted tools binary path relative to `root` (defaults to `Config.projectLocation`) |
| `GetToolsIntDir(projName, root=None)` | Returns the formatted tools intermediate path relative to `root` |

This separation keeps tool build artifacts isolated from regular project outputs.

---

## Project Loading & Dependency Resolution

The `ProjectManager` module handles loading project definitions and resolving their dependency graphs.

### Loading Flow

1. `LoadProjects(projectList)` iterates the provided list of project paths.
2. For each path, `LoadProject(projPath)`:
   - Skips if the project was already loaded (deduplication by folder path).
   - Creates a `Project` object with name derived from the path basename.
   - Calls `Reload()`, which dynamically imports the project's `Build.py` script via `importlib.util` and calls its exported `GetProject()` function.
   - Copies all properties from the returned `Project` into the manager's instance.
   - Applies default includes (the project's own `src/` folder) and defines (`CONFIGURATION`, `_UNICODE`, `UNICODE`, `_CRT_SECURE_NO_WARNINGS`, `PROJECT_NAME`).
   - Resolves relative include paths to absolute paths.
   - If `genReflection=True`, automatically appends `"Reflection"` to `moduleLinks`.
3. For each entry in the project's `modulePaths`, the manager:
   - Resolves the module location via `SearchForModulePath()`, which checks four candidate locations in order:
     1. `<Config.location>/modules/<modulePath>` (Frost root modules)
     2. `<Config.location>/<modulePath>` (Frost root)
     3. `<project-folder>/modules/<modulePath>` (project local modules)
     4. `<project-folder>/<modulePath>` (relative to project)
   - Recursively calls `LoadProject()` on the resolved path.

### Dependency Collection

The `Project` class provides methods for collecting transitive dependencies:

| Method | Description |
|--------|-------------|
| `CollectAllModules()` | BFS traversal of `moduleLinks`; returns list of all transitive dependency projects |
| `CollectIncludes()` | Aggregates `includes` and `externIncludes` from all modules in the dependency chain |
| `CollectSourceFiles()` | Auto-discovers `.c`/`.cpp` in `src/` plus files matched by `externalSources` globs |
| `CollectHeaderFiles()` | Collects `.h`/`.hpp` from `src/` |
| `CollectLinks()` | Aggregates direct linker names and resolves static library outputs for each module link |

### Project Registry

All loaded projects are stored in the global `projects: dict[str, Project]` registry keyed by full folder path. `GetProject(name)` looks up a project in two steps:

1. **Primary lookup**: Folder-path suffix match (`proj.folder.endswith(projName)`) - this is the intended path for `-p` arguments and works with absolute or relative paths resolved to absolute.
2. **Fallback lookup**: Basename match against `proj.projectName` - fires only if the first lookup fails.

The function prints `"project X is not defined"` and returns `None` if neither lookup matches.

### Cyclic Dependency Detection

After all projects are loaded and their `SetupProject()` hooks have run, the build system performs a cycle detection check via `CheckCyclicDependencies()`. This uses depth-first search with three colours (WHITE/GRAY/BLACK) over the `moduleLinks` dependency graph. If a back-edge to a GRAY node is found, the cycle path is reconstructed and printed:

```
________________________________________________________________________________
Error: Cyclic dependency detected among projects!
________________________________________________________________________________

Cycle: ProjectA -> ProjectB -> ProjectC -> ProjectA

A project cannot depend on itself through a chain of moduleLinks.
Check the Build.py scripts for the projects above and remove the cycle.
________________________________________________________________________________
```

The build exits with code 1 when a cycle is detected, preventing infinite recursion in `CollectAllModules()` and `BuildProject()`.

---

## Two-Stage Builds

The build system supports two execution stages defined by `Config.BuildStage`:

| Stage | Value | Purpose |
|-------|-------|---------|
| `EARLY` | 0 | Codegen tools that must exist before normal projects compile (e.g., HeaderTool, CodeGen) |
| `NORMAL` | 1 | Standard application and library projects |

### Execution Order in Build.py

```python
# Stage 1: Build all EARLY-stage projects, each after the early projects it links
for proj in ProjectManager.InBuildOrder(buildable, Config.BuildStage.EARLY):
    BuildProject(proj, Config.BuildStage.EARLY)

# Stage 2: Build NORMAL-stage projects
if no specific project requested:
    # Full build - build all normal projects
    for proj in ProjectManager.projects.values():
        BuildProject(proj, Config.BuildStage.NORMAL)
else:
    # Single project build - only the requested project
    proj = ProjectManager.GetProject(Config.projectLocation)
    BuildProject(proj, Config.BuildStage.NORMAL)
```

The `BuildProject()` function checks `proj.buildStage != buildStage` and returns early for non-matching projects, ensuring strict stage ordering. `ProjectManager.InBuildOrder` puts the early projects after the early projects they link (`moduleLinks`), whatever order they loaded in, so a clean tree builds with plain `-b`. In full-build mode (`-fb`), dependency resolution recursively builds linked modules before the target project.

---

## Compilation Pipeline

Each project's compilation is orchestrated by `ProjectBuilder.ProjectEnvironment`, which delegates to two sub-systems: ObjectBuilder (compilation) and Linker (linking).

### ObjectBuilder

Located in `Builder/ObjectBuilder.py`, this module compiles individual C++ source files using Clang's `-cc1` frontend.

#### Compilation Process

For each source file, ObjectBuilder:

1. **Constructs compiler arguments** including:
   - Target triple: `x86_64-pc-windows-msvc`
   - C++ standard: `-std=c++20`
   - Include paths from `CollectIncludes()` and `GetSysIncludes()`
   - Project defines and global defines
   - Configuration-specific optimization flags
   - Per-project `compilerArgs`

2. **Dependency tracking**: Generates `.d` tree files for each source to track header dependencies. Cached `.args` files enable incremental compilation - if neither the source file nor its dependency arguments have changed since the last build, recompilation is skipped.

3. **Parallel execution**: Uses `ThreadPoolExecutor` to compile multiple source files concurrently.

   **Crash retry**: clang 22.1.0 and HeaderTool (libclang) crash intermittently under the parallel build on
   Windows, on a different file each time and never reproducibly on a re-run. When a compile or a HeaderTool run
   exits with a crash code (0xC0000005 access violation, 0xC00000FD stack overflow, 0xC0000409 stack buffer
   overrun), `RunRetryingCrashes` runs it again, up to twice, printing
   `warning: <tool> crashed (<reason>) on <file>; retrying (n/2)`. An ordinary error exit is never retried.

4. **Reflection pipeline** (when `genReflection=True`): After successful compilation of a source file, the HeaderTool analyzes headers and emits reflection metadata. Generated code in `generated/generated.cpp` is also compiled into an object file.

#### Output

Each compiled source produces a `.obj` file in the project's intermediate directory (`bin-int/{config}-{system}-{arc}/{projName}/`).

#### Stale Intermediate File Cleanup

The linker takes every `.obj` in the intermediate directory, so the object of a source that was deleted or renamed would still be linked. Before compiling, `ProjectBuilder.BuildSources()` calls `RemoveStaleIntermediates`, which runs `CleanStaleIntermediateFiles(expectedNames, intDir)` once for the project: it removes every `.obj`, `.d` and `.args` file whose base name is not one of the project's sources or generated sources (`generated/*.cpp`, reflection and custom code generators), keeps `linker.args`, and prints `removed stale intermediate: <path>` for each file it removes.

A compile that fails (a non-zero exit, or success without an object) deletes the `.obj` and `.d` it may have written, so the next build compiles the file again: a compiler that crashed after writing its object would otherwise leave an object newer than the source, which the next build would link.

An object is named after its source's base name, so two sources with the same base name in one project would overwrite each other's object. `SharedObjectNames(sources)` finds them and the build fails with an error naming both.

### Compile Database

Every build writes `compile_commands.json` at the root project's folder (`Utils/CompileDatabase.py`), so clangd (IED's C/C++ plugin, an editor's) knows each source's flags. The cc1 flags Frost compiles with mean nothing to such a tool, so an entry is the clang driver's form of the same compile: `--target`, the language and standard, the Microsoft compatibility and exception flags, the include folders (system ones as `-isystem`), the defines, and the project's own `-W`/`-f`/`-m`/`-D`/`-U` flags. `ObjectBuilder.BuildSources` records each source it is given (compiled or up to date); when the build process ends the entries are merged into the file, replacing those of the same sources and keeping the rest, so building one project leaves the others' entries. The file and clangd's `.cache/` are ignored by git.

### Linker

Located in `Builder/Linker.py`, this module links object files into final outputs using Clang with LLD (for executables and DLLs) or `llvm-lib` (for static libraries).

#### Link Process

1. **Collects all `.obj` files** from the intermediate directory for the project.
2. **Resolves library dependencies** via `CollectLinks()`, which includes:
   - Direct entries from the project's `links` list
   - Static library outputs (`.lib`) resolved from each entry in `moduleLinks`
3. **Performs incremental link checks**: Compares output file mtime against object files and linker args cache; skips linking if the output is already up-to-date.
4. **Invokes the linker** through a response file (`linker.rsp` in the intermediate directory, POSIX quoting): a module's object paths in a deep folder, such as a git worktree, outgrow the 32767 characters of a Windows command line. The flags depend on `BuildType`:
   - `EXECUTABLE` -> Clang + LLD, outputs `.exe`
   - `STATIC_LIBRARY` -> `llvm-lib`, outputs `.lib`
   - `DYNAMIC_LIBRARY` -> Clang + LLD with `-fPIC`, outputs `.dll`

#### Post-Build Actions

After a successful build, two optional lifecycle hooks are called in order:

1. **`OnAfterBuild()`** - Called after C++ compilation and linking succeed but before `OnProjectBuilt()`. Intended for custom build steps that run against the compiled output, such as building script code (e.g., C# assemblies) or other language-specific compilation pipelines.
2. **`OnProjectBuilt()`** - Called last after all build and copy actions complete. Used for post-build distribution tasks such as copying DLLs, data files, or the project's own output to target locations.

Additionally, file copies defined in `copies` and `selfCopies` are executed before both hooks.

---

## Build Info & Metrics

The build system tracks per-file and per-project timing metrics during every build. Metrics are recorded in `Builder/BuildInfo.py` and persisted to JSON after each successful build run.

### Metrics Data Model

Two dataclasses capture build metrics:

**FileBuildInfo** - Per-source-file compilation metrics:

| Field | Type | Description |
|-------|------|-------------|
| `sourcePath` | `str` | Source path relative to the project folder |
| `compileNeeded` | `bool` | Whether compilation was actually performed (vs. skipped as up-to-date) |
| `compileTime` | `float` | Compilation duration in seconds |
| `compileStartTime` | `float` | Wall-clock start (`time.time()`) |
| `compileEndTime` | `float` | Wall-clock end (`time.time()`) |
| `reflectionCollectNeeded` | `bool` | Whether HeaderTool reflection collection was performed |
| `reflectionCollectTime` | `float` | Reflection collection duration in seconds |
| `reflectionCollectStartTime` | `float` | Wall-clock start of reflection collection |
| `reflectionCollectEndTime` | `float` | Wall-clock end of reflection collection |
| `buildSucceeded` | `bool` | Whether the file compiled without errors |

**ProjectBuildInfo** - Per-project aggregate metrics:

| Field | Type | Description |
|-------|------|-------------|
| `projectName` | `str` | Project name |
| `configuration` | `str` | Build configuration (`Debug`, `Development`, `Dist`) |
| `architecture` | `str` | Target architecture |
| `timestamp` | `str` | ISO-8601 build timestamp |
| `totalBuildTime` | `float` | Total wall-clock time for the project build (seconds) |
| `buildStartTime` / `buildEndTime` | `float` | Wall-clock start and end of project build |
| `compileTime` | `float` | Sum of all file compile times (seconds) |
| `reflectionGenTime` | `float` | CodeGen tool invocation time (seconds) |
| `reflectionCompileTime` | `float` | Time to compile `generated/generated.cpp` (seconds) |
| `linkTime` | `float` | Linker invocation time (seconds) |
| `compileNeeded` / `reflectionGenNeeded` / `linkNeeded` | `bool` | Whether each step was actually performed |
| `numFilesCompiled` | `int` | Files compiled this run |
| `numFilesSkipped` | `int` | Files up-to-date, skipped |
| `numFilesFailed` | `int` | Files whose compilation returned non-zero |
| `files` | `list[FileBuildInfo]` | Per-file breakdown |

Metrics are accumulated in the global `_build_session: dict[str, ProjectBuildInfo]` registry. Key functions:

| Function | Description |
|----------|-------------|
| `GetBuildSession()` | Return the current session dictionary |
| `ClearBuildSession()` | Reset all session state |
| `CreateProjectInfo(name, config, arch)` | Create and register a new `ProjectBuildInfo` |
| `GetProjectInfo(name)` | Retrieve metrics for a project by name |
| `RegisterFile(name, file_info)` | Attach a `FileBuildInfo` under a project |

### Saving and Loading Build Info

After every successful build, `Build.py` automatically calls `SaveBuildInfo()` to write the session to a JSON file:

- **Path**: `<outputDir>/build-info.json` (typically inside the target project's intermediate directory)
- **Format**: Top-level keys are `"buildTime"` (ISO-8601 timestamp) and `"projects"` (dictionary of project names to `ProjectBuildInfo.to_dict()` output)

Previous build info can be loaded with `LoadBuildInfo(input_path)`, which returns `None` on any error.

### Chrome Tracing Export

After each build, `SaveChromeTrace()` writes a Chrome tracing JSON file (`<outputDir>/build-trace.json`) that can be loaded in `chrome://tracing` or `ui.perfetto.dev` for interactive timeline visualisation.

The trace uses the earliest `buildStartTime` across all projects as T=0 and emits the following event categories:

| Category | Event Name | Scope | Description |
|----------|-----------|-------|-------------|
| `build` | `Project Build` | Per-project | Overall project build span |
| `compile` | `Compile` | Per-file | Individual source file compilation (each file on its own thread slice to show parallelism) |
| `reflection` | `Reflection Collect` | Per-file | HeaderTool reflection collection for a single source |
| `reflection` | `Reflection Generate` | Per-project | CodeGen tool invocation |
| `reflection` | `Reflection Compile` | Per-project | Compiling `generated/generated.cpp` |
| `link` | `Link` | Per-project | Linker invocation |

Each event includes duration in microseconds and relevant timing args.

### Compile Time Reporting (-ci)

Passing `-ci` sets `Config.printCompileInfo = True`, which triggers `PrintCompileInfo()` after compilation completes but before linking. This prints a formatted report to the console:

**Per-file table:**

```
====================================================================================================
Compile Time Information
====================================================================================================
File                                                                        Time (s)
----------------------------------------------------------------------------------------------------
src/Engine/Renderer/Renderer.cpp                                             0.1234
src/Engine/Scene/Scene.cpp                                                   0.0891
...
----------------------------------------------------------------------------------------------------
Total                                                                       2.4567
```

**Per-folder summary:**

```
Folder                                                                        Files    Total (s)      Avg (s)
----------------------------------------------------------------------------------------------------
src/Engine/Renderer                                                            12       1.2345       0.1029
src/Engine/Scene                                                               8        0.6789       0.0849
...
----------------------------------------------------------------------------------------------------
Overall                                                                       20       1.9134       0.0957
====================================================================================================
```

This helps identify slow-to-compile files and folders for optimisation targeting.

---

## Visual Studio Integration

The build system generates Visual Studio 2022 solution and project files for IDE integration via `Utils/VSGenerator.py`, while delegating actual build execution back to the Python build system.

### VS Generator

`Utils/VSGenerator.py` directly produces `.sln` and `.vcxproj` files from Python without requiring external tools.

#### What Gets Generated

- **Solution file** (`.sln`) containing all loaded projects grouped by their `group` property (dots converted to `/` for VS folder nesting).
- **Project files** (`.vcxproj`) per project, using NMake-style `ConfigurationType=Makefile`, delegating Build/Rebuild/Clean to `Build.py`. Includes:
  - All source files from `CollectSourceFiles()` and `CollectHeaderFiles()`
  - Include paths from the full dependency chain
  - Preprocessor defines for IntelliSense
- **Filter files** (`.vcxproj.filters`) with directory-based filter trees derived from paths relative to `src/`.
- **User files** (`.vcxproj.user`) with per-configuration debugger settings (`LocalDebuggerCommand`).

All GUIDs are deterministic, generated via SHA-256 hashing of content keys (project names, group paths, filter paths), ensuring stable VS tracking across regeneration runs.

#### Build Delegation

Visual Studio build/rebuild/clean actions are wired through NMake commands that invoke `Build.py` with appropriate flags. This means:
- Building from VS uses the Python build system (Clang compilation, dependency tracking, reflection pipeline).
- The IDE provides code navigation and IntelliSense via the generated project files.

---

## Test Execution

Unit test projects are flagged with `isUnitTestProject = True` in their `Build.py` script. **Test projects are never root projects** - there is exactly one root project in the workspace, and all compilation happens in context to that single root.

### Key Constraints

- **Single Root Project**: Only one project in the entire workspace has `isRootProject = True`. This ensures all binaries and intermediates are located in one place relative to that root.
- **Conditional Compilation**: Test projects (`isUnitTestProject = True`) are **never** compiled during normal builds (`-b`, `-fb`). They only compile when explicitly requested via `-test`, `-benchmark`, or `-testall`.
- **Exception Safety**: The test runner catches exceptions (including `std::exception` and unknown exceptions) from individual tests so a crashing test does not disrupt the remaining test suite.

### CLI Flags

| Flag | Description |
|------|-------------|
| `-test -p <root>` | Build all dependencies, compile test projects, then run all unit tests in the root project's dependency tree. |
| `-benchmark -p <root>` | Build all dependencies, compile test projects, then run all benchmarks in the root project's dependency tree. |
| `-testall -p <root>` | Discover and run all test projects in the root project's dependency tree. Implicitly does a full build including test projects. |

### How It Works

1. **Root Project Requirement**: All three flags require `-p <root-project>` specifying the single root executable. The specified project is validated as a root project via `ValidateTargetProject()`.
2. **Test Discovery** (`Build.py`): `FindTestProjectsForRoot()` traverses the root project's full dependency tree using `CollectAllModules()` and returns every project with `isUnitTestProject = True`. Results are sorted alphabetically for deterministic ordering.
3. **Conditional Compilation**: When test flags are active, the build loop includes unit test projects. During normal builds, they are skipped entirely.
4. **Execution**: Each discovered test executable is run sequentially via `RunTestProject()`. Exit codes are aggregated - if any test returns non-zero, that code becomes the final exit code.
5. **Implicit Build**: `-test`, `-benchmark`, and `-testall` automatically set `shouldBuild = True` and `fullBuild = True` so all dependencies compile first, including the test projects themselves.

### Test Filtering

Test executables support filtering via command-line arguments passed to the binary:

```
CoreTesting.exe --filter "Vector.*"
```

The filter uses wildcard matching (`*` for any sequence, `?` for single character) against fully qualified test names (e.g., `VectorTest.Vector.Constructors.TestDefaultConstructor`). Multiple filters can be combined.

### Exception and Crash Handling

The test runner wraps every test invocation in try/catch blocks:

```cpp
try {
    entry.func->Invoke(entry.ownerInstance, {});
}
catch (const TestFailedException&) {
    // Failure already reported by assertion macro
}
catch (const std::exception& e) {
    s_FailedTests++;
    // Print crash message with exception details
}
catch (...) {
    s_FailedTests++;
    // Print crash message for unknown exceptions
}
```

This applies to both regular tests (`TestBase.cpp`) and filtered tests (`TestFilter.cpp`), as well as benchmarks (`BenchmarkFilter.cpp`). A crashing test increments the failure count but does not abort the remaining test suite.

### Example Commands

```powershell
# Run all tests for C4's dependency tree (builds first, including test projects)
python Build.py -test -p C:/Work/C4 -c Debug -a x64

# Run all benchmarks for C4's dependency tree
python Build.py -benchmark -p C:/Work/C4 -c Development -a x64

# Same as -test - run all tests in the root project's tree
python Build.py -testall -p C:/Work/C4 -c Debug -a x64
```

A descriptive error is printed and the build exits with code 1 if `-p` is omitted:
```
Error: A root project must be specified with -p when running tests.
Example: python Build.py -test -p <path-to-root-project> -c Debug -a x64
```

### Normal Builds Exclude Test Projects

When building without test flags, unit test projects are skipped entirely:

```powershell
# This builds only the root project and its non-test dependencies
python Build.py -fb -c Debug -a x64 -p C:/Work/C4

# Test projects (CoreTesting, ReflectionTesting, C4Testing) are NOT compiled
```

---

## Project Definition Pattern

Every project is defined by a `Build.py` script in its root folder that exports a `GetProject()` function returning a configured `Project` object.

### Required: GetProject() Function

```python
from ProjectManager import Project
import Config

def GetProject():
    proj = Project()
    proj.projectName = "MyProject"
    proj.group = "MyGroup"
    proj.buildType = Config.BuildType.EXECUTABLE
    # ... configure properties
    return proj
```

### Optional Lifecycle Hooks

```python
def DownloadDependencies():
    """Called during SetupProject() - download external dependencies."""
    pass

def OnAfterBuild():
    """Called after C++ compilation and linking succeed but before OnProjectBuilt().
    Intended for custom build steps such as building script code (e.g., C# assemblies)."""
    pass

def OnProjectBuilt():
    """Called last, after all build and copy actions complete - post-build actions."""
    pass
```

### Project Properties Reference

| Property | Type | Description |
|----------|------|-------------|
| `projectName` | `str` | Unique project name (defaults to folder basename; a name the script sets wins, so a checkout's folder name does not matter) |
| `group` | `str` | VS solution group; dots converted to `/` for nesting |
| `externalSources` | `list[str]` | Extra source files outside `src/` (supports `*`, `**` globs) |
| `includes` | `list[str]` | User include directories |
| `sysIncludes` | `list[str]` | System include directories (`-internal-isystem`) |
| `externIncludes` | `list[str]` | External includes propagated to dependent projects |
| `modulePaths` | `list[str]` | Sub-project module paths loaded recursively |
| `moduleLinks` | `list[str]` | Project names to link against (resolves to `.lib`) |
| `links` | `list[str]` | Direct library names or paths for the linker |
| `defines` | `list[str]` | Preprocessor definitions (project-local) |
| `globalDefines` | `list[str]` | Definitions propagated to all projects |
| `vsDefines` | `list[str]` | Definitions added only to VS IntelliSense patching |
| `compilerArgs` | `list[str]` | Extra Clang compiler flags |
| `linkerArgs` | `list[str]` | Extra linker flags |
| `copies` | `list[dict\|str]` | Files or folders to copy to output directory |
| `selfCopies` | `list[str]` | Target paths for copying the project's own output |
| `buildType` | `Config.BuildType` | Output type: `EXECUTABLE`, `STATIC_LIBRARY`, `DYNAMIC_LIBRARY`, `UNBUILT_LIBRARY` |
| `genReflection` | `bool` | Enable automatic reflection codegen (default: `True`) |
| `buildStage` | `Config.BuildStage` | Build ordering: `EARLY` or `NORMAL` |
| `isRootProject` | `bool` | Mark this project as a root project that can be directly targeted for build/run (default: `False`) |
| `isUnitTestProject` | `bool` | Mark this project as a unit test executable for discovery by `-testall` (default: `False`) |

### Source File Resolution

The build system auto-discovers `.c` and `.cpp` files in the project's `src/` folder. Additional sources use glob patterns in `externalSources`:
- `folder/*.ext` - all matching files in a directory
- `folder/**/*.ext` - recursive match under a directory

For example, `Projects/Core/Modules/yaml/Build.py` compiles the yaml-cpp submodule with
`externalSources = ["yaml-cpp/src/**/*.cpp"]` and `includes = ["yaml-cpp/include"]`.

### Reflection Codegen Pipeline

When `genReflection=True`, each compiled source file triggers the reflection pipeline:

1. Compile source -> `.obj` via Clang `-cc1`
2. Run HeaderTool on source file to extract reflection data
3. Compile `generated/generated.cpp` -> `.obj`
4. Link all objects into final output

The Reflection module is automatically added to `moduleLinks` when reflection is enabled.

### Root Projects

A **root project** is an executable project that can be directly targeted with `-p` for build (`-b`, `-fb`, `-rebuild`) or run (`-r`). The `isRootProject` flag restricts which executables the user can target, preventing accidental direct builds of tool executables (e.g., HeaderTool, CodeGen) and test harnesses that should only be built as transitive dependencies.

**Invariant: There is exactly ONE root project in the entire workspace.** All compilation happens in context to that single root project, meaning all binaries and intermediates are located relative to it. Unit test projects (`isUnitTestProject = True`) must NEVER be marked as root projects.

The `-p` argument must be a **filesystem path** to the project folder, not a short project name. It is resolved to an absolute path via `os.path.abspath()`.

```python
def GetProject():
    proj = Project()
    proj.buildType = Config.BuildType.EXECUTABLE
    proj.isRootProject = True  # Allow direct targeting with -p
    return proj
```

```powershell
# Correct - absolute path to the single root project
python Build.py -fb -c Debug -a x64 -p C:/Work/C4

# Correct - relative path (resolved against Frost directory)
python Build.py -fb -c Debug -a x64 -p ../C4
```

If a project is targeted directly but `isRootProject` is `False`, the build system prints an error and exits:

```
Error: 'CoreTesting' is not marked as a root project.
Only root projects can be directly targeted for build or run. Set 'isRootProject = True' in the project's Build.py script.
```

---

## Command Reference

**Important:** The `-p` flag requires a filesystem path to the project folder, not a short project name. Paths are resolved to absolute paths by `os.path.abspath()`. Relative paths must be correct relative to the current working directory (which is set to the Frost directory at startup).

```powershell
# Dependencies (LLVM, Python, submodules): once, from the root of the IED repository
setup.bat

# Generate Visual Studio project files
python Build.py -gs

# Full build - the C4 compiler and all dependencies (using absolute path)
python Build.py -fb -c Debug -a x64 -p C:/Work/C4

# Rebuild from scratch (clean + build)
python Build.py -rebuild -c Debug -a x64 -p C:/Work/C4

# Clean build artifacts
python Build.py -clean -c Debug -a x64 -p C:/Work/C4

# Run the built executable after building
python Build.py -r -c Debug -a x64 -p C:/Work/C4

# Build with compile time information report
python Build.py -b -c Debug -a x64 -p C:/Work/C4 -ci

# Run all tests for C4's dependency tree
python Build.py -test -p C:/Work/C4 -c Debug -a x64
```
