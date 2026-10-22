# Frost - Documentation Hub

Frost is the build system for the C4 compiler and its C++ support libraries (Core, Reflection,
ObjectDescription, Profiling, Threading, UnitTesting). The C4 tree's root `Build.py` is its root project.

Frost downloads nothing itself: its dependencies come from `setup.bat` at the root of the IED repository (the
folder above the C4 tree), which installs the pinned LLVM release into `vendor/Compiler/`, a private Python
into the repository's `vendor/python/`, and the git submodules. `Build.py` exits with "run setup.bat" when
`vendor/Compiler/` is missing.

## Folder Structure

```
Frost/
|-- Build.py                  # Main build entry point - CLI parsing, project loading, orchestration
|-- Config.py                 # Global build configuration (configs, compiler paths, defaults)
|-- ProjectManager.py         # Project loading, module resolution, dependency collection
|
|-- Builder/                  # Build pipeline - compile and link stages
|   |-- ObjectBuilder.py      # Clang source compilation with dependency tracking
|   |-- Linker.py             # Object linking into executables or libraries
|   `-- ProjectBuilder.py     # Per-project build orchestration (sources, reflection, link)
|
|-- Utils/                    # Utility modules
|   |-- VSGenerator.py        # Direct Visual Studio solution and project file generation
|   |-- Utils.py              # File resolution, directory helpers, target triple
|   `-- Windows.py            # Windows SDK/MSVC detection, include/lib path discovery
|
|-- Projects/                 # Buildable projects managed by the build system
|   |-- Core/                 # Core library - Vector, String, UUID, Ref<T>, assertions
|   |   `-- Modules/yaml/     # yaml-cpp: the git submodule yaml-cpp/, compiled through Build.py
|   |-- CoreTesting/          # Core library unit tests
|   |-- ObjectDescription/    # Object description and serialization system
|   |-- Profiling/            # Profiling library: the IED Trace Protocol client (Itp.h, Itp.cpp)
|   |-- Reflection/           # C++ reflection codegen framework
|   |   |-- CodeGen/          # Reflection code generator (runs post-compile)
|   |   |-- HeaderTool/       # Header analysis tool for reflection extraction
|   |   |-- Reflection/       # Runtime reflection library
|   |   `-- StaticReflection/ # Static reflection support
|   `-- UnitTesting/          # Unit testing framework
|
|-- vendor/                   # Not committed; installed by setup.bat at the repository root
|   `-- Compiler/             # Clang 22.1.0 LLVM toolchain
|
`-- docs/                     # Documentation
    |-- BuildSystem.md        # Build pipeline overview: compilation, linking, project resolution
    |-- CoreLibrary.md        # Core library documentation: Vector, String, UUID, Ref<T>, assertions
    |-- MathLibrary.md        # Math library: vector/matrix types, transformations, utilities
    |-- ReflectionSystem.md   # Compile-time metadata extraction and runtime type introspection
    `-- UnitTesting.md        # Unit testing framework: test/benchmark discovery, history, regression
```

Build output goes to the root project's folder: `bin/` (final output, per configuration) and `bin-int/`
(intermediates: `.obj`, `.d`, `.args` files).

yaml-cpp (`Projects/Core/Modules/yaml/yaml-cpp`, built from `externalSources = ["yaml-cpp/src/**/*.cpp"]`) is a git
submodule (the fork AlanBuechner/yaml-cpp); `setup.bat` (or `git submodule update --init`) checks it out. The glm
copies (`Projects/CoreTesting/src/glm`) stay vendored. Profiling (`ENABLE_PROFILING`) streams to the IED profiler
through the IED Trace Protocol client in `Projects/Profiling/src/Profiling/Itp.h` (IED `docs/16`); Tracy is gone.

## Key Systems

| System | Location | Description |
|--------|----------|-------------|
| **Project Manager** | `ProjectManager.py` | Loads project definitions from per-project `Build.py` scripts. Resolves module paths, manages dependency graphs, collects includes and links across projects. Central registry of all known projects. |
| **Object Builder** | `Builder/ObjectBuilder.py` | Compiles C++ source files to `.obj` using Clang's `-cc1` frontend. Tracks dependencies via `.d` tree files and cached `.args` files for incremental builds. Runs reflection header analysis after successful compilation. Parallelized via `ThreadPoolExecutor`. A compile or HeaderTool run whose tool **crashes** (exit 0xC0000005 access violation, 0xC00000FD stack overflow, 0xC0000409 stack buffer overrun) is retried up to twice with `warning: <tool> crashed (...) on <file>; retrying (n/2)`: clang 22.1.0 and libclang crash intermittently under the parallel build on Windows, never reproducibly. An ordinary error exit is never retried. An object is stamped with the time its sources were read, so a source saved during its compile is compiled again (BUG-346); `Frost/tests` checks the incremental rules against a stub compiler (`python -m unittest discover -s C4/Frost/tests`). |
| **Linker** | `Builder/Linker.py` | Links object files into final outputs (`.exe`, `.lib`, `.dll`) using Clang with LLD or llvm-lib. Supports executables, static libraries, and dynamic libraries. Performs incremental link checks based on output file mtime and linker args cache. |
| **VS Generator** | `Utils/VSGenerator.py` | Generates Visual Studio 2022 solution and NMake-style project files directly from Python. Uses deterministic content-based GUIDs for stable VS tracking. Collects all source files, include paths, defines, and module links per project. Delegates build/rebuild/clean to the Python build system through `Build.bat`. |
| **Unit Testing Framework** | `Projects/UnitTesting/` | Reflection-driven test and benchmark harness with zero-boilerplate discovery, colored console output, hierarchical grouping, persistent benchmark history with regression detection, and automatic pruning of old snapshots. |

## Build Configuration

### Compiler
- **Clang/LLVM 22.1.0** - the pinned LLVM release, installed into `vendor/Compiler/` by `setup.bat` at the repository root. Runs via `-cc1` frontend for compilation and LLD for linking executables.
- Target triple: `x86_64-pc-windows-msvc` (MSVC-compatible ABI)

### C++ Standard
- **C++20** (`-std=c++20`) - All projects compile with C++20 features enabled.

### Configurations

| Configuration | Optimization | Debug Info | Description |
|---------------|-------------|------------|-------------|
| `Debug` | `-O0` | CodeView standalone, full PDBs | Unoptimized debug builds with full diagnostics |
| `Development` | `-O3` | CodeView standalone, full PDBs | Optimized development builds (default) |
| `Dist` | `-O3` | PDBs only if `useDistPDBs=True` | Distribution/release builds |

### Architecture and System
- **Architecture**: `x64` (only supported architecture; alias: `x86_64`)
- **System**: `windows` (Windows-only target)

### Output Directories
- Bin: `bin/{config}-{system}-{arc}/{projName}/`
- Intermediate: `bin-int/{config}-{system}-{arc}/{projName}/`

### Build Types (`Config.BuildType`)
| Type | Value | Output Extension | Description |
|------|-------|-----------------|-------------|
| `EXECUTABLE` | 0 | `.exe` | Linked executable with LLD |
| `STATIC_LIBRARY` | 1 | `.lib` | Static library via llvm-lib |
| `DYNAMIC_LIBRARY` | 2 | `.dll` | Shared library with `-fPIC` |
| `UNBUILT_LIBRARY` | 3 | - | Header-only or source-only; no build step |

### Build Stages (`Config.BuildStage`)
- **EARLY** - Built before normal projects (e.g., codegen tools)
- **NORMAL** - Standard build stage for application projects

## Build Commands

All commands run from the **Frost** directory using `python Build.py` (the C4 tree's `Build.bat` runs the same
`Build.py` with the repository's Python and passes its arguments through).

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
```

### CLI Arguments

| Argument | Description | Values |
|----------|-------------|--------|
| `-gs` | Generate Visual Studio project files | flag |
| `-b` | Build the specified project | flag |
| `-fb` | Full build - project and all dependencies | flag |
| `-rebuild` | Clean then rebuild | flag |
| `-clean` | Remove intermediate and generated files | flag |
| `-r` | Run the built executable | flag |
| `-p` | Target project - absolute or relative filesystem path resolved to absolute (e.g. `C:/Work/C4`) |
| `-c` | Configuration | `Debug`, `Development`, `Dist` |
| `-a` | Architecture | `x64`, `x86_64` (alias) |
| `-s` | System | `windows` |

### Optional Flags (`Config.py`)
- `useAsan = True/False` - Enable AddressSanitizer
- `useDistPDBs = True/False` - Include PDBs in Dist configuration

## Project Definition Pattern

Each project is defined by a `Build.py` script in its root folder. The script must export a `GetProject()` function that returns a configured `Project` object.

### Required: `GetProject()` Function

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

### Optional: Lifecycle Hooks

```python
def DownloadDependencies():
    """Called during project setup - download external dependencies."""
    pass

def OnAfterBuild():
    """Called after C++ compilation and linking succeed but before OnProjectBuilt().
    Intended for custom build steps such as building script code (e.g., C# assemblies)."""
    pass

def OnProjectBuilt():
    """Called last, after all build and copy actions complete - post-build actions."""
    pass
```

### Project Properties

| Property | Type | Description |
|----------|------|-------------|
| `projectName` | `str` | Unique project name (defaults to folder basename; a name the script sets wins) |
| `group` | `str` | VS solution group; dots converted to `/` for nesting |
| `externalSources` | `list[str]` | Extra source files outside `src/` (supports `*`, `**` globs) |
| `includes` | `list[str]` | User include directories |
| `sysIncludes` | `list[str]` | System include directories (passed as `-internal-isystem`) |
| `externIncludes` | `list[str]` | External includes collected into dependent projects |
| `modulePaths` | `list[str]` | Sub-project module paths to load recursively |
| `moduleLinks` | `list[str]` | Project names to link against (resolves to `.lib`) |
| `links` | `list[str]` | Direct library names or paths for the linker |
| `defines` | `list[str]` | Preprocessor definitions per-project |
| `globalDefines` | `list[str]` | Definitions propagated to all projects |
| `vsDefines` | `list[str]` | Definitions added only to VS IntelliSense patching |
| `compilerArgs` | `list[str]` | Extra Clang compiler flags |
| `linkerArgs` | `list[str]` | Extra linker flags |
| `copies` | `list[dict\|str]` | Files or folders to copy to output directory |
| `selfCopies` | `list[str]` | Target paths for copying the project's own output |
| `buildType` | `Config.BuildType` | Output type: executable, static lib, dynamic lib, unbuilt |
| `isUnitTestProject` | `bool` | Mark as a unit test executable for `-testall` discovery (default: `False`) |
| `genReflection` | `bool` | Enable automatic reflection codegen (default: `True`) |
| `buildStage` | `Config.BuildStage` | Build ordering: `EARLY` or `NORMAL` |
| `isRootProject` | `bool` | Mark as a root project that can be directly targeted with `-p` (default: `False`) |

### Source File Resolution

The build system auto-discovers `.c` and `.cpp` files in the project's `src/` folder. Additional sources use glob patterns in `externalSources`:
- `folder/*.ext` - all matching files in a directory
- `folder/**/*.ext` - recursive match under a directory

### Reflection Codegen Pipeline

When `genReflection = True`, each compiled source file triggers the HeaderTool to analyze headers and emit reflection metadata. Generated code is compiled from `generated/generated.cpp`. The pipeline:
1. Compile source -> `.obj` via Clang `-cc1`
2. Run HeaderTool on source file to extract reflection data
3. Compile `generated/generated.cpp` -> `.obj`
4. Link all objects into final output

### Root Projects

Only projects with `isRootProject = True` can be directly targeted with `-p` for build or run. This prevents accidentally targeting tool executables (e.g., HeaderTool, CodeGen) or test harnesses. **There is exactly one root project in the workspace.** Unit test projects (`isUnitTestProject = True`) are NEVER root projects and are only compiled when explicitly requested via `-test`, `-benchmark`, or `-testall`.

## Python Coding Standards

### Language
- **Python 3.x** - Uses type hints (`list[str]`, `str | os.PathLike[str]`), f-strings, and modern stdlib features.
- No external dependencies beyond the Python standard library.

### Naming Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Modules / Files | `PascalCase.py` or `snake_case.py` | `ProjectManager.py`, `Utils.py` |
| Classes | `PascalCase` | `Project`, `ObjectBuilder`, `ProjectEnvironment` |
| Functions / Methods | `snake_case` | `BuildSources()`, `CollectLinks()` |
| Local variables | `snake_case` | `args`, `result`, `startTime` |
| Constants / Config | `snake_case` at module level | `configurations`, `cppVersion` |
| Private attributes | `_` prefix | `_needsBuild`, `_lastBuildTime` |

### Style and Structure
- **Type hints** on function signatures where the return type or parameter types benefit from clarity.
- **Docstrings** on public functions and methods using triple-quote format.
- **Imports** grouped at top: standard library first, then internal modules via `from ... import ...` or `import ...`.
- **f-strings** preferred for string formatting.
- **Forward slashes** used in all path strings (backslashes replaced via `.replace("\\", "/")`).
- Enum classes (`Enum`) for fixed sets of values (`BuildType`, `BuildStage`, etc.).
- Mutable default state avoided; project objects initialized with empty lists/dicts in `__init__`.

### Path Handling
- All internal paths normalized to forward slashes.
- `os.path.join()` used for path construction; `.replace("\\", "/")` applied when storing.
- Absolute paths preferred after resolution; relative paths resolved against project folder.

## C++ Coding Standards

### StringFormat / Logging Placeholder Syntax - CRITICAL

**Under absolutely zero circumstances use bare `{}` as a format placeholder.**

The `Core::StringFormat` system (used by `FormatString()`, all `CORE_*` and `DEBUG_*` logging macros) requires **numbered, zero-indexed placeholders**: `{0}`, `{1}`, `{2}`, etc.

Bare `{}` without a number is **INVALID**. The parser calls `std::from_chars` on an empty string, which fails with `invalid_argument`, causing `ParseMessage` to return `false` and the entire format operation to abort silently. The raw message string is output with no substitution.

```cpp
// CORRECT - numbered placeholders
CORE_INFO("Player {0} scored {1} points", playerName, score);
String s = FormatString("File {0} has {1} bytes", path, size);

// WRONG - bare {} will NOT work under any circumstances
CORE_INFO("Player {} scored {} points", playerName, score);  // SILENT FAILURE
```

This applies to:
- `Core::StringFormat::Format(...)`
- `Core::FormatString(...)`
- `CORE_TRACE`, `CORE_INFO`, `CORE_WARN`, `CORE_ERROR`
- `DEBUG_TRACE`, `DEBUG_INFO`, `DEBUG_WARN`, `DEBUG_ERROR`

### Comments - Keep Them Minimal

Code should be self-documenting through clear names and structure. Only add a comment when the code itself cannot convey the reason or intent.

- No paragraph-long explanations for what a function does - the signature should make it clear.
- No `///` doc-comment blocks on every method unless there's actual ambiguity to resolve.
- Section dividers (`// ---`) and groupings are noise, not documentation. Avoid them.
- If something needs explanation, one short line is enough. If it takes more than two lines of comment, the code probably needs restructuring.
