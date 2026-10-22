# Unit Testing Framework

The **UnitTesting** framework is a lightweight, reflection-driven test and benchmark harness among the C++ libraries of IED's C/C++ plugin, which the C4 compiler's tests use. It provides zero-boilerplate test discovery through compile-time metadata extraction, colored console output, hierarchical grouping, persistent benchmark history with regression detection, and automatic pruning of old snapshots.

The framework lives in `plugins/cpp/libraries/UnitTesting/` and is consumed as a library by test executables such as **CoreTesting**.

---

## Table of Contents

- [Architecture](#architecture)
  - [Project Layout](#project-layout)
  - [Dependencies](#dependencies)
  - [High-Level Flow](#high-level-flow)
- [Test System](#test-system)
  - [Base Class](#base-class)
  - [Macros](#macros)
  - [Discovery](#discovery)
  - [Grouping](#grouping)
  - [Console Output](#console-output)
- [Benchmark System](#benchmark-system)
  - [Base Class](#base-class-1)
  - [Macros](#macros-1)
  - [Discovery](#discovery-1)
  - [BenchmarkRunner](#benchmarkrunner)
  - [Results Table](#results-table)
  - [Supporting Types](#supporting-types)
- [Benchmark History Persistence](#benchmark-history-persistence)
  - [Configuration](#configuration)
  - [Snapshot Format](#snapshot-format)
  - [API](#api)
  - [Regression Comparison](#regression-comparison)
  - [Baseline Pinning](#baseline-pinning)
  - [Pruning and Cleanup](#pruning-and-cleanup)
- [Build Configuration](#build-configuration)
  - [UnitTesting Library](#unittesting-library)
  - [Test Executable](#test-executable)
  - [Entry Point](#entry-point)

---

## Architecture

### Project Layout

```
plugins/cpp/libraries/UnitTesting/
├── nuke.toml             # the Nuke project: depends on Core, Reflection, ObjectDescription
├── src/
│   └── UnitTesting/
│       ├── Testing.h/cpp           # Public entry point: RunTestsAndBenchmarks()
│       ├── TestBase.h/cpp          # Unit test base class, macro definitions, runner
│       ├── BenchmarkTest.h/cpp     # Benchmark base class, macro definitions, runner
│       └── BenchmarkHistory.h/cpp  # Persistent benchmark history (JSON snapshots)
```

### Dependencies

The UnitTesting library links against three core projects:

| Dependency | Purpose |
|------------|---------|
| **Core** | `Core::String`, `Core::Vector`, `Core::BenchmarkRunner`, `Core::BenchmarkResult`, logging utilities, timing (`Stopwatch`) |
| **Reflection** | Compile-time metadata extraction (`CLASS()`, `FUNCTION()`), runtime registry, attribute-based discovery |
| **ObjectDescription** | JSON serialization/deserialization for benchmark history persistence |

The library itself does **not** generate reflection code. Test executables that consume it reflect (`[project.driver] reflection = true`) so their `CLASS()` and `FUNCTION()` macros produce metadata.

### High-Level Flow

```
main()
  └── Testing::RunTestsAndBenchmarks()
        ├── RunTests()
        │     ├── Discover test classes from Reflect::Registry group "Testing"
        │     ├── Instantiate each class, collect tagged methods into hierarchical TestGroup tree
        │     ├── Invoke each test method via reflection
        │     └── Print colored summary (Total / Passed / Failed)
        │
        └── RunBenchmarks()
              ├── Discover benchmark classes from Reflect::Registry group "Benchmarking"
              ├── Instantiate each class, collect tagged methods into hierarchical BenchmarkGroup tree
              ├── Invoke each benchmark method via reflection
              ├── Collect BenchmarkResult objects
              └── Print formatted results table (Name, Mean, Median, Min, Max, Iters)
```

Both runners follow a **two-phase** pattern:
1. **Discovery & Collection** — Scan the reflection registry, instantiate test/benchmark classes, and build hierarchical group trees from method attributes.
2. **Execution** — Walk the tree, invoke each function via `Reflect::Function::Invoke()`, accumulate results.

Instances are kept alive in a local vector during execution and deleted after printing summaries.

---

## Test System

### Base Class

All unit test suites inherit from `Testing::Test`, which itself derives from `Reflect::Reflected`:

```cpp
namespace Testing
{
    class Test : public Reflect::Reflected
    {
    public:
        virtual ~Test() = default;

    protected:
        static void TestPassed();

        template<typename ... Args>
        void TestFailed(const Core::String& msg = "", const Args&... args);
    };
}
```

The `Reflect::Reflected` base enables the class to be registered in the reflection registry, instantiated via `CreateInstance()`, and have its methods inspected for attributes.

### Macros

Three macros simplify writing test methods:

| Macro | Purpose | Expands To |
|-------|---------|------------|
| `TEST_FUNC(group)` | Tag a method as a test belonging to a hierarchical group | `FUNCTION(TestGroup=group)` |
| `TEST_FUNC_TIMEOUT(group, seconds)` | The same, for a test allowed to run longer than the default 5 seconds before it is stopped as hung (one that builds and runs a program) | `FUNCTION(TestGroup=group, TestTimeoutSeconds=seconds)` |
| `TEST_PASSED()` | Signal that the current test passed | Increments pass counter, prints `"Passed"` in green, returns |
| `TEST_FAILED(...)` | Signal that the current test failed with an optional formatted message | Increments fail counter, prints `"Failed: <msg>"` in red, returns |

The group string is a **dot-separated path** (e.g., `"Vector.Constructors"`) that creates a nested hierarchy at runtime.

**Example:**

```cpp
namespace Testing
{
    CLASS(GROUP = Testing) VectorTest : public Test
    {
    public:
        REFLECTED_BODY(Testing::VectorTest);

        TEST_FUNC(Vector.Constructors)
        void TestDefaultConstructor();

        TEST_FUNC(Vector.Accessors)
        void TestOperatorIndex();

        TEST_FUNC(Vector.Mutations)
        void TestPushBackElement();
    };
}
```

The `CLASS(GROUP = Testing)` macro registers the class in the `"Testing"` reflection group, making it discoverable by the test runner. The `REFLECTED_BODY` macro completes the reflection registration.

### Discovery

Test discovery is entirely **reflection-driven** and requires no explicit registration:

1. `RunTests()` queries `Reflect::Registry::GetRegistry()->GetGroup("Testing")` for all reflected classes in the `"Testing"` group.
2. For each class, it calls `CreateInstance()` to allocate a live object.
3. It iterates the class's reflected functions and filters by the presence of a `"TestGroup"` attribute (injected by `TEST_FUNC`).
4. The attribute value is a dot-separated group path (e.g., `"Vector.Constructors"`) which is split into segments and used to navigate or create nodes in a hierarchical `TestGroup` tree.
5. Each matching function is stored as a `TestEntry` pairing the `Reflect::Function*` with the owning instance pointer.

Any reflected class registered in the `"Testing"` group is considered a test suite candidate.

### Grouping

Tests are organized in a **hierarchical tree** of `TestGroup` objects:

```cpp
struct TestGroup
{
    Core::String name;
    Core::Vector<TestGroup> subGroups;
    Core::Vector<TestEntry> tests;
};
```

The group path from `TEST_FUNC("A.B.C")` creates (or reuses) nested sub-groups `A → B → C`, placing the test as a leaf under `C`. This produces indented, tree-like console output.

### Console Output

Test output is printed to `std::cout` with **ANSI-colored** formatting via `Logging::Logger::SetConsoleColor()`:

- **Group headers** are printed at each level: `Group: <name>`
- **Test names** are left-aligned in a column whose width matches the longest test name across all groups.
- **Passed** results appear in green (`Info` color).
- **Failed** results appear in red (`Error` color) with the failure message.

After all tests run, a summary block is printed:

```
========================================
Test Summary
  Total:  42
  Passed: 40
  Failed: 2
========================================
```

The failed count is highlighted in red when greater than zero.

---

## Benchmark System

### Base Class

Benchmark suites inherit from `Testing::BenchmarkTest`, also deriving from `Reflect::Reflected`:

```cpp
namespace Testing
{
    class BenchmarkTest : public Reflect::Reflected
    {
    public:
        virtual ~BenchmarkTest() = default;

    protected:
        static void BenchmarkPassed(const Core::BenchmarkResult& result);

        template<typename ... Args>
        void BenchmarkFailed(const Core::String& msg = "", const Args&... args);
    };
}
```

### Macros

Three macros mirror the test system for benchmarks:

| Macro | Purpose | Expands To |
|-------|---------|------------|
| `BENCHMARK_FUNC(group)` | Tag a method as a benchmark belonging to a hierarchical group | `FUNCTION(BenchmarkGroup=group)` |
| `BENCHMARK_PASSED(result)` | Submit a successful `BenchmarkResult` for collection | Pushes result into the global results vector, returns |
| `BENCHMARK_FAILED(...)` | Signal that the benchmark failed with an optional formatted message | Increments fail counter, prints `"Failed: <msg>"` in red, returns |

**Example:**

```cpp
namespace Testing
{
    CLASS(GROUP = Benchmarking) CoreBenchmarks : public BenchmarkTest
    {
    public:
        REFLECTED_BODY(Testing::CoreBenchmarks);

        BENCHMARK_FUNC(Benchmarks.Vector)
        void BenchVectorPushBack();

        BENCHMARK_FUNC(Benchmarks.Hash)
        void BenchStringHash();
    };
}

void Testing::CoreBenchmarks::BenchVectorPushBack()
{
    Core::BenchmarkResult result = Core::BenchmarkRunner::RunBenchmark(
        "VectorPushBack", []() {
            Core::Vector<int32> vec;
            vec.Reserve(1024);
            for (int32 i = 0; i < 1024; ++i)
                vec.Push(i);
        });
    BENCHMARK_PASSED(result);
}
```

### Discovery

Benchmark discovery mirrors test discovery but targets the **`"Benchmarking"`** registry group:

1. `RunBenchmarks()` queries `Reflect::Registry::GetRegistry()->GetGroup("Benchmarking")`.
2. Each class is instantiated and its functions scanned for the `"BenchmarkGroup"` attribute (injected by `BENCHMARK_FUNC`).
3. Dot-separated group paths build a hierarchical `BenchmarkGroup` tree.
4. Matching functions are stored as `BenchmarkEntry` objects pairing the reflected function with its owning instance.

The `CLASS(GROUP = Benchmarking)` macro places the class into the correct registry group.

### BenchmarkRunner

Actual timing is delegated to `Core::BenchmarkRunner`, a **static-only** utility in the Core library:

```cpp
namespace Core
{
    /// Configuration for auto-scaling benchmark execution.
    struct BenchmarkConfig
    {
        double targetWallTime = 1.0;      // Target total wall time in seconds
        uint32 initialIterations = 16;     // Starting iteration count before scaling
        uint32 maxIterations = 1048576;    // Safety cap on iterations
        double growthFactor = 2.0;         // Geometric growth factor per scaling round
        double minWallTime = 0.1;          // Minimum wall time to stop scaling
    };

    class BenchmarkRunner
    {
    public:
        /// Run a benchmark with geometric iteration scaling to reach target wall time.
        static BenchmarkResult RunBenchmark(
            const String& testName,
            const std::function<void()>& func,
            const BenchmarkConfig& config = {});

        /// Compare two results and flag regression based on a percentage threshold.
        static BenchmarkCompareResult Compare(
            const BenchmarkResult& resultA,
            const BenchmarkResult& resultB,
            double regressionThreshold = 5.0);
    };
}
```

`RunBenchmark` executes the provided callable with **geometric iteration scaling**:

1. Start with `initialIterations` (default 16).
2. If total wall time is below `minWallTime`, multiply iterations by `growthFactor` and repeat.
3. Stop when wall time approaches `targetWallTime` or `maxIterations` is reached.
4. Compute per-iteration statistics from all timing samples.

Results are captured in a `Core::BenchmarkResult`:

```cpp
struct BenchmarkStats
{
    double mean = 0.0;
    double median = 0.0;
    double stddev = 0.0;
    double minTime = std::numeric_limits<double>::max();
    double maxTime = std::numeric_limits<double>::lowest();
    uint32 iterations = 0;

    /// Compute statistics from a collection of per-iteration timing values (seconds).
    void ComputeFromSamples(const std::vector<double>& samples);
};

struct BenchmarkResult
{
    String name;
    BenchmarkStats stats;
    double totalWallTime = 0.0;

    /// Convenience: run a benchmark function N times and fill this result.
    static BenchmarkResult Run(const String& testName, const std::function<void()>& func, uint32 iterations);
};
```

### Results Table

After all benchmarks complete, a formatted table is printed:

```
========================================
Benchmark Results
  Total:      8
  Completed:  7
  Failed:     1
========================================

Name                     Mean(s)   Median(s)     Min(s)     Max(s)    Iters
StringFormat           0.000012    0.000011    0.000008    0.000045      1200
VectorResize           0.000034    0.000032    0.000025    0.000120       800
...
========================================
```

Columns are:
- **Name** — Left-aligned benchmark name (24 chars).
- **Mean(s)** — Arithmetic mean per-iteration time in seconds.
- **Median(s)** — Median per-iteration time.
- **Min(s)** — Fastest iteration.
- **Max(s)** — Slowest iteration.
- **Iters** — Total measured iterations.

Results are also accessible programmatically via `Testing::GetBenchmarkResults()`, returning the global `Core::Vector<Core::BenchmarkResult>` from the last `RunBenchmarks()` call.

### Supporting Types

The Core library provides additional benchmarking utilities in `BenchmarkResult.h`:

#### BenchmarkComparison

Compares two results, expressing relative performance change:

```cpp
struct BenchmarkComparison
{
    String nameA;
    String nameB;
    double meanA = 0.0;
    double meanB = 0.0;
    double ratio = 0.0;              // B/A (>1 means B is slower, <1 means B is faster)
    double percentChange = 0.0;      // ((B - A) / A) * 100

    static BenchmarkComparison Compare(const BenchmarkResult& resultA, const BenchmarkResult& resultB);
};
```

#### BenchmarkCompareResult

Extends `BenchmarkComparison` with regression flagging:

```cpp
struct BenchmarkCompareResult
{
    BenchmarkComparison comparison;
    bool isRegression = false;        // True if B is slower than A beyond threshold
    double thresholdPercent = 0.0;
};
```

#### HistoricalBenchmarkData

Tracks a sequence of results for a single benchmark over time:

```cpp
struct HistoricalBenchmarkData
{
    String benchmarkName;
    std::vector<BenchmarkResult> history;

    void AddResult(const BenchmarkResult& result);
    double GetBaselineMean() const;   // Earliest recorded mean
    double GetLatestMean() const;     // Most recent recorded mean
    double RegressionPercent() const; // Percent change from baseline to latest
};
```

#### Stopwatch

Lightweight timer wrapping `Core::Time::GetTime()`:

```cpp
class Stopwatch
{
public:
    void Start();        // Start or restart (accumulates existing elapsed)
    void Stop();         // Stop and record elapsed
    void Reset();        // Discard all state
    double GetElapsedSeconds() const;       // Live if running, else recorded
    double GetElapsedMilliseconds() const;
    bool IsRunning() const;
};
```

---

## Benchmark History Persistence

The `Core::BenchmarkHistory` class provides **file-based persistence** of benchmark results as timestamped JSON snapshots, enabling regression detection across builds.

### Configuration

```cpp
struct BenchmarkHistoryConfig
{
    fs::path historyDirectory = "benchmark-history";  // Output directory
    uint32 maxSnapshots = 64;                          // Retention limit (0 = unlimited)
    double defaultRegressionThreshold = 5.0;           // Default regression threshold (%)
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `historyDirectory` | `"benchmark-history"` | Directory where JSON snapshot files are stored. Created automatically if it doesn't exist. |
| `maxSnapshots` | `64` | Maximum number of JSON files to retain. Oldest files are pruned after each save. Set to `0` for unlimited retention. |
| `defaultRegressionThreshold` | `5.0` | Default percentage threshold for regression detection when `CompareWithHistory` is called without an explicit threshold. |

### Snapshot Format

Each benchmark result is saved as a single JSON file named `benchmark-YYYYMMDD-HHMMSS-mmm.json`:

```json
{
    "timestamp": "2026-06-29T10:12:45",
    "label": "abc1234 - feature/string-perf",
    "result": {
        "name": "StringFormat",
        "totalWallTime": 0.1452,
        "stats": {
            "mean": 0.000012,
            "median": 0.000011,
            "stddev": 0.000003,
            "minTime": 0.000008,
            "maxTime": 0.000045,
            "iterations": 1200
        }
    }
}
```

Serialization uses the `ObjectDescription` system (`SaveObjectDescriptionToJson` / `LoadObjectDescriptionFromJsonFile`) for structured JSON output. Filenames embed millisecond timestamps to guarantee uniqueness.

### API

| Method | Return | Description |
|--------|--------|-------------|
| `BenchmarkHistory(config)` | — | Constructor. Creates the history directory if needed. |
| `SaveResult(result, label)` | `String` | Saves a benchmark result as a new timestamped JSON file. Returns the file path or empty string on failure. Automatically prunes old snapshots after saving. |
| `LoadAll()` | `Vector<BenchmarkHistoryEntry>` | Loads all historical entries from disk, sorted oldest-first by filename (timestamp-based sorting). |
| `LoadLatest()` | `optional<BenchmarkHistoryEntry>` | Returns the most recent entry, or `std::nullopt` if no snapshots exist. |
| `CompareWithHistory(result, threshold)` | `BenchmarkHistoryComparison` | Compares a current result against the latest (and optionally pinned) history entry. |
| `PinBaseline(timestamp)` | — | Pins a specific historical entry as the comparison baseline by its ISO-8601 timestamp string. |
| `UnpinBaseline()` | — | Clears the pinned baseline. |
| `GetPinnedTimestamp()` | `const String&` | Returns the currently pinned timestamp, or empty string if none. |
| `PruneOldSnapshots()` | `uint32` | Removes oldest snapshots beyond `maxSnapshots`. Returns count removed. |
| `ClearHistory()` | `uint32` | Deletes all JSON files in the history directory. Returns count removed. |

Copy and move constructors are deleted — `BenchmarkHistory` is a non-copyable singleton-style manager.

### Regression Comparison

`CompareWithHistory` returns a `BenchmarkHistoryComparison` struct:

```cpp
struct BenchmarkHistoryComparison
{
    std::optional<BenchmarkHistoryEntry> latestEntry;
    std::optional<BenchmarkHistoryEntry> pinnedBaseline;

    double percentChangeVsLatest = 0.0;   // ((current - latest) / latest) * 100
    double percentChangeVsPinned = 0.0;   // ((current - pinned) / pinned) * 100

    bool regressionVsLatest = false;      // true if percentChange > threshold
    bool regressionVsPinned = false;      // true if percentChange > threshold

    double thresholdPercent = 0.0;
};
```

**Interpretation:**
- **Positive percentage change** means the current benchmark is **slower** (regression).
- **Negative percentage change** means the current benchmark is **faster** (improvement).
- A regression is flagged when the positive percentage change exceeds the configured threshold.
- If no history exists, all fields remain at their defaults (zero/false/empty).

### Baseline Pinning

Pinning allows locking comparison to a specific historical snapshot rather than always comparing against the latest:

```cpp
Core::BenchmarkHistory history;

// Pin to a known-good baseline
history.PinBaseline("2026-06-15T09:30:00");

// Now CompareWithHistory will include percentChangeVsPinned and regressionVsPinned
auto comp = history.CompareWithHistory(currentResult);
if (comp.regressionVsPinned) {
    CORE_WARN("Regression detected vs pinned baseline!");
}

// Unpin when done
history.UnpinBaseline();
```

The pinned timestamp must match an existing entry's `timestamp` field exactly. If no matching entry is found, the pinned comparison fields remain at defaults.

### Pruning and Cleanup

- **Automatic pruning** — `SaveResult()` calls `PruneOldSnapshots()` after writing a new snapshot. Files are sorted by name (which embeds the timestamp) and the oldest are removed first until `maxSnapshots` files remain.
- **Manual pruning** — Call `PruneOldSnapshots()` directly to enforce retention limits.
- **Full cleanup** — `ClearHistory()` removes all JSON files in the history directory.

When `maxSnapshots` is `0`, pruning is disabled and all snapshots are retained indefinitely.

---

## Build Configuration

### UnitTesting Library

The UnitTesting project is `plugins/cpp/libraries/UnitTesting/nuke.toml`:

```toml
[project]
name = "UnitTesting"
kind = "library"
language = "cpp"
group = "C++ Libraries"
depends = ["Core", "Reflection", "ObjectDescription"]
```

Core provides types and `BenchmarkRunner`; Reflection provides discovery; ObjectDescription provides JSON
serialization for history. The library has no classes to reflect; test projects do the reflecting.

### Test Executable

A test executable (e.g., **CoreTesting**) depends on UnitTesting, reflects so that `CLASS()` and `FUNCTION()` macros
produce metadata, and says it is a test program:

```toml
[project]
name = "CoreTesting"
kind = "executable"
language = "cpp"
depends = ["Core", "Threading", "UnitTesting", "ObjectDescription"]
includes = ["src/glm"]
tests = true

[project.driver]
reflection = true
```

The executable must include all test and benchmark headers so the reflection pipeline processes them. See
CoreTesting's `Source.cpp` for the pattern: every test/benchmark header is `#include`d before `main()`.

### Entry Point

A minimal test executable entry point (from CoreTesting):

```cpp
#define IMPLEMENT_LOGGER
#include "Core/Core.h"
#include "UnitTesting/Testing.h"
#include "UnitTesting/BenchmarkHistory.h"

// Include all test headers so the reflection pipeline processes them
#include "Tests/Math/ScalarMathTest.h"
#include "Tests/Containers/VectorTest.h"
// ... more includes ...
#include "Benchmarks/CoreBenchmarks.h"

int main(int argc, char* argv[])
{
    Testing::RunTestsAndBenchmarks();
    return 0;
}
```

The `#define IMPLEMENT_LOGGER` is required before including `Core/Core.h` to instantiate the logger singleton. All test and benchmark headers must be included so the reflection codegen pipeline processes their `CLASS()` and `FUNCTION()` macros — without inclusion, the classes won't appear in the registry and discovery will skip them.

Alternatively, run phases separately:

```cpp
Testing::RunTests();        // Unit tests only
Testing::RunBenchmarks();   // Benchmarks only
```

After `RunBenchmarks()`, results are accessible via `Testing::GetBenchmarkResults()` for programmatic use (e.g., saving to history or CI reporting).

### Integration with Build System

- **Test programs**: a C++ executable with `tests = true` is a test program. `nuke test` builds it (with what it
  depends on) and runs it from its project folder; with no project named, every test program of the workspace runs,
  and the exit code is 1 when any failed. A plain `nuke build` builds a test program only when it is named or
  something depends on it.
- **Exception Safety**: The test runner catches exceptions from individual tests so a crashing test does not disrupt
  the remaining suite.

```powershell
# Every test program (C4Testing, CoreTesting, ReflectionTesting in the IED repository)
nuke test

# One, filtered: Nuke passes -filter=<glob> to the program
nuke test CoreTesting --filter "Vector.*"

# Benchmarks: arguments after -- go to the program
nuke test CoreTesting -- -benchmark-filter=VectorPushBack
```

#### Test Filtering

Test executables support filtering via command-line arguments:

```
CoreTesting.exe -filter=Vector.*
CoreTesting.exe -benchmark-filter=VectorPushBack
CoreTesting.exe -list-tests=1
CoreTesting.exe -list-benchmarks=1
```

The filter uses wildcard matching (`*` for any sequence, `?` for single character) against fully qualified test names (e.g., `VectorTest.Vector.Constructors.TestDefaultConstructor`).

#### Exception and Crash Handling

The test runner wraps every test invocation in try/catch blocks to prevent one crashing test from aborting the entire suite:

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

This applies to:
- Regular tests (`TestBase.cpp::RunTestGroup`)
- Filtered tests (`TestFilter.cpp::RunFilteredTests`)
- Benchmarks (`BenchmarkFilter.cpp::RunFilteredBenchmarks`)

A crashing test increments the failure count but does not abort the remaining test suite.

The reflection codegen pipeline (HeaderTool) automatically extracts `CLASS()` and `FUNCTION()` metadata from compiled test source files, populating the registry groups that `RunTests()` and `RunBenchmarks()` scan at runtime.