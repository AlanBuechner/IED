# The C++ libraries of the C/C++ plugin

The C++ libraries the C4 compiler and its tools are built on, shipped with IED's C/C++ plugin so any Nuke workspace can
depend on them by name (Nuke/README.md section 9c): before 2026-10-21 they were Frost's `Projects/`. Each folder is a
Nuke project (`nuke.toml`, `language = "cpp"`, group `C++ Libraries`), built by the plugin's driver (`../nuke/driver`).

| Project | Kind | What |
|---|---|---|
| `Core` | library | containers (`Vector`, `Map`), `String`, UUID, `Ref<T>`, assertions, logging, time, `ArgList`, the Math library; depends on `yaml` |
| `yaml` | library | yaml-cpp, the git submodule `yaml/yaml-cpp` (the owner's fork) |
| `Threading` | library | threads, jobs, synchronization |
| `Profiling` | library | the IED Trace Protocol client (`src/Profiling/Itp.h`, `Itp.cpp`; IED `docs/16`) and `Performance.h`'s macros (`ENABLE_PROFILING`) |
| `Reflection` | library | runtime type information; a project with `[project.driver] reflection = true` depends on it, the HeaderTool and CodeGen |
| `StaticReflection` | library | the reflection data the HeaderTool and CodeGen share |
| `HeaderTool` | executable | reads a reflecting project's sources with libclang and writes their reflection data |
| `CodeGen` | executable | gathers that data into the project's `generated.cpp` |
| `ObjectDescription` | library | object descriptions and their YAML/JSON serialization |
| `ChangeNotification` | library | change notifications over reflected objects |
| `UnitTesting` | library | the test and benchmark harness: a test program takes `-filter=GLOB`, `-benchmark-filter=GLOB`, `-list-tests=1`, `-list-benchmarks=1` |
| `CoreTesting`, `ReflectionTesting` | test programs | their tests: `nuke test CoreTesting ReflectionTesting` |

Docs: `../docs/` (CoreLibrary, MathLibrary, ReflectionSystem, UnitTesting). The glm copy in `CoreTesting/src/glm` stays
vendored (locally modified 0.9.9.6).

## C++ standards

The repository's standards (minimal comments, honest functions, no global mutable state, one level of abstraction per
function, small files, real test coverage: `C4/AGENTS.md`) apply. Two rules of these libraries on top:

**StringFormat placeholders are numbered.** `Core::StringFormat` (`FormatString()`, every `CORE_*` and `DEBUG_*`
logging macro) needs zero-indexed placeholders, `{0}`, `{1}`. A bare `{}` makes the parser's `std::from_chars` fail on
an empty string, and the whole format silently prints the raw message with no substitution.

```cpp
CORE_INFO("Player {0} scored {1} points", playerName, score);   // right
CORE_INFO("Player {} scored {} points", playerName, score);     // silently wrong
```

**Tests with the library.** A change to a library comes with a test in its test program (`CoreTesting` for Core,
Threading and Profiling; `ReflectionTesting` for the reflection libraries, ObjectDescription and ChangeNotification), and
`nuke test CoreTesting ReflectionTesting` passes, as `nuke gates` of the IED repository checks.
