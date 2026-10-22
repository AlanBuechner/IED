# Core Library

The Core Library is the foundational C++20 library of IED's C/C++ plugin (`plugins/cpp/libraries/`), shared by the C4 compiler and its tools. It provides essential utilities used by every other project - type aliases, containers, formatting, logging, memory tracking, threading, platform abstraction, and more.

- **Location:** `plugins/cpp/libraries/Core/src/` (the Nuke project `Core`, `language = "cpp"`)
- **Reflection codegen:** none (no `reflection = true`)

---

## Table of Contents

- [Core.h Umbrella Header](#coreh-umbrella-header)
- [Type Aliases](#type-aliases)
- [String Types](#string-types)
- [Vector Container](#vector-container)
- [Map and Set Aliases](#map-and-set-aliases)
- [SparseMap Container](#sparsmap-container)
- [Smart Pointer - Ref\<T\>](#smart-pointer---reft)
- [Assertion Macros](#assertion-macros)
- [Utility Macros](#utility-macros)
- [String Formatting](#string-formatting)
- [Logging Hierarchy](#logging-hierarchy)
- [Performance and Profiling](#performance-and-profiling)
- [Benchmarking Utilities](#benchmarking-utilities)
- [Time Utilities](#time-utilities)
- [UUID Generation](#uuid-generation)
- [Memory Tracker](#memory-tracker)
- [Threading Primitives](#threading-primitives)
- [Platform Abstraction](#platform-abstraction)
- [Bit Flags](#bit-flags)
- [Dynamic Bit Allocator](#dynamic-bit-allocator)
- [Hash - Compile-Time CRC32](#hash---compile-time-crc32)
- [Argument List](#argument-list)
- [UTF-8 and Encoding Utilities](#utf-8-and-encoding-utilities)
- [Math Module](#math-module)

---

## Core.h Umbrella Header

`Core/Core.h` is the primary inclusion point for the Core library. It transitively pulls in:

| Included Header | Purpose |
|----------------|---------|
| `Math/Math.h` | Math types and functions (custom implementation) |
| `Platform/PlatformCommon.h` | Cross-platform utilities |
| `Core/Common.h` | Common macros and traits |
| `Core/Utils.h` | String encoding helpers |
| `Core/Types.h` | Fixed-width type aliases |
| `Core/Vector.h` | Custom vector container |
| `Core/SparseMap.h` | Dense/sparse hash map |
| `Core/Logging/Log.h` | Logging hierarchy |

> **Note:** `MemTracker/MemTracker.h` and `Performance.h` were relocated to the **Profiling** project (`plugins/cpp/libraries/Profiling/`). Include them via `Profiling/MemTracker/MemTracker.h` and `Profiling/Performance.h` when needed; they are no longer transitively included by `Core/Core.h`.

### Platform Detection

`Core.h` defines exactly one platform macro at compile time:

```cpp
#define PLATFORM_WINDOWS    // Currently the only supported platform
// #define PLATFORM_IOS      // Unsupported - compile error
// #define PLATFORM_MACOS    // Unsupported - compile error
// #define PLATFORM_ANDROID  // Unsupported - compile error
// #define PLATFORM_LINUX    // Unsupported - compile error
```

Attempting to build on an unsupported platform produces a hard `#error`.

---

## Type Aliases

**File:** `Core/Types.h`

The library defines fixed-width integer and character type aliases for consistent sizing across compilers:

| Alias | Underlying Type | Description |
|-------|----------------|-------------|
| `uint8` | `unsigned char` | 8-bit unsigned |
| `uint16` | `unsigned short` | 16-bit unsigned |
| `uint32` | `unsigned int` | 32-bit unsigned |
| `uint64` | `unsigned long long` | 64-bit unsigned |
| `int8` | `signed char` | 8-bit signed |
| `int16` | `signed short` | 16-bit signed |
| `int32` | `signed int` | 32-bit signed |
| `int64` | `signed long long` | 64-bit signed |
| `byte` | `unsigned char` | Byte alias |
| `char8` | `char8_t` | UTF-8 character |
| `char16` | `char16_t` | UTF-16 character |
| `char32` | `char32_t` | UTF-32 character |

---

## String Types

**File:** `Core/String.h`

Convenience aliases for standard C++ string types:

| Alias | Underlying Type |
|-------|----------------|
| `Core::String` | `std::string` |
| `Core::StringView` | `std::string_view` |
| `Core::WString` | `std::wstring` |
| `Core::WStringView` | `std::wstring_view` |
| `Core::StringStream` | `std::stringstream` |

---

## Vector Container

**File:** `Core/Vector.h`

`Core::Vector<T>` is a thin wrapper around `std::vector<T>` that provides a consistent API with engine conventions. It inherits privately from `std::vector<T>`, so it is **not** implicitly convertible to the standard type (use `.CastToSTD()` when needed).

### Key Differences from `std::vector`

| Feature | `Core::Vector` | `std::vector` |
|---------|---------------|--------------|
| Size/Capacity return type | `uint32` | `size_t` |
| Indexing | `operator[](uint32)` | `operator[](size_t)` |
| Push returns reference | `Type& Push(...)` returns the pushed element | `void push_back(...)` |
| Pop returns value | `Type Pop()` returns and removes back element | `void pop_back()` |
| Find returns index | `uint32 Find(val)` returns index or `Count()` on miss | No built-in find by value |
| Swap-and-pop removal | `SwapAndPop(index/pos)` for O(1) unordered removal | Not available |

### Constructors

```cpp
Vector();                              // Empty
Vector(uint32 count);                  // Count default-constructed elements
Vector(uint32 count, const Type& val); // Count copies of val
Vector(std::initializer_list<Type>);   // Initializer list
Vector(const Vector& other);           // Copy
Vector(Vector&& other) noexcept;       // Move
```

### Element Access

| Method | Description |
|--------|-------------|
| `operator[](uint32 index)` | Unchecked element access |
| `Front()` / `Back()` | First/last element reference |
| `Data()` | Raw pointer to internal storage |

### Modification

| Method | Description |
|--------|-------------|
| `Push(value&)` / `Push(value&&)` | Append; returns reference to appended element |
| `Pop()` | Remove and return back element |
| `Insert(index, value)` / `Insert(iterator, value)` | Insert at position |
| `Append(Vector& other)` | Concatenate another vector (reserves ahead) |
| `Remove(index/pos/iterator)` | Erase element; shifts remaining elements |
| `RemoveRange(start, end)` | Erase a range of elements |
| `SwapAndPop(index/pos/iterator)` | O(1) unordered removal by swapping with back |
| `Clear()` | Remove all elements |
| `Reserve(capacity)` / `ReserveMore(n)` | Pre-allocate storage |
| `Resize(size, value)` | Change size, filling new elements with `value` |

### Query

| Method | Description |
|--------|-------------|
| `Count()` | Number of elements (`uint32`) |
| `Capacity()` | Allocated capacity (`uint32`) |
| `Empty()` / `Full()` | Is empty / is at capacity |
| `ElementSize()` | `sizeof(T)` (constexpr) |
| `SizeInBytes()` | Total bytes used by elements |
| `Find(val)` | Index of first matching element, or `Count()` if not found |
| `FindIf(predicate)` | Index of first element satisfying predicate, or `Count()` |
| `Contains(val)` | Whether the vector contains the value |

### Iteration

Standard iterator support: `.begin()`, `.end()`, and range-for loops.

---

## Map and Set Aliases

**File:** `Core/Map.h`

```cpp
template<typename K, typename V> using Map = std::map<K, V>;
template<typename T> using Set = std::set<T>;
```

These provide `Core::Map` and `Core::Set` as namespace-scoped aliases.

---

## SparseMap Container

**File:** `Core/SparseMap.h`

`Core::SparseMap<KeyType, ValueType>` is a dense/sparse hash map keyed by integral type (e.g., entity IDs). It provides O(1) insert, lookup, and delete with compact contiguous value storage. Keys are used as direct indices into the sparse table, which grows as needed.

```cpp
template<std::integral KeyType, typename ValueType>
class SparseMap
{
public:
    struct Entry { KeyType key; ValueType value; };
};
```

### Insertion and Access

| Method | Return | Description |
|--------|--------|-------------|
| `Insert(key, value)` | `ValueType&` | Insert or update; returns reference to stored value |
| `Insert(key)` | `ValueType&` | Insert default-constructed value; returns reference |
| `Push(value)` | `KeyType` | Push entry reusing first free slot; returns assigned key |
| `Push()` | `KeyType` | Push default-constructed entry; returns assigned key |
| `operator[](key)` | `ValueType&` / `const ValueType&` | Mutable/const access (undefined if key does not exist) |

### Removal and Query

| Method | Return | Description |
|--------|--------|-------------|
| `Remove(key)` | `bool` | Remove entry; returns `true` if the key existed |
| `Contains(key)` | `bool` | Check whether the key exists in the map |
| `Count()` | `uint32` | Number of live entries |
| `Empty()` | `bool` | Whether the map has no entries |
| `Dense()` | `const Vector<Entry>&` | Access to the dense array of live entries |

### Capacity and Lifecycle

| Method | Description |
|--------|-------------|
| `Reserve(capacity)` | Pre-allocate storage for the dense array |
| `Clear()` | Remove all entries; keeps allocated memory |

### Internal Structure

The sparse map maintains two arrays:

- **`m_Dense`** (`Core::Vector<Entry>`) - Contiguous storage of live `{key, value}` pairs. Removal uses swap-and-pop to keep this array compact.
- **`m_Sparse`** (`Core::Vector<KeyType>`) - Direct index-by-key lookup array mapping `key -> dense slot index` or `-1` (invalid). Grows automatically when a key exceeds current capacity.

---

## Smart Pointer - Ref\<T\>

**File:** `Core/Core.h` (defined inline)

`Ref<T>` is the engine's primary reference-counted smart pointer. Objects that participate in shared ownership inherit from `Refable`.

### Refable Base

```cpp
class Refable {
public:
    uint32 GetRefCount();  // Current reference count
private:
    std::atomic<uint32> m_Count = 0;
    // AddRef/Release are private - only accessible via Ref<T>
};
```

### Ref\<T\> Usage

```cpp
// Create a managed object
auto obj = CreateRef<MyClass>(arg1, arg2);

// Access the raw pointer
MyClass* ptr = obj.Get();

// Member access
obj->SomeMethod();

// Check validity
if (obj) { /* valid */ }
bool isValid = obj.Valid();

// Reset (release reference; deletes if refcount hits 0)
obj.Reset();

// Unchecked cast (no type check - faster)
Ref<Derived> derived = base.As<Derived>();

// Checked cast (dynamic_cast - returns null Ref on failure)
Ref<Derived> derived = base.Cast<Derived>();

// Pointer cast (returns raw pointer, nullptr on failure)
Derived* ptr = base.PointerCast<Derived>();
```

### Key Behaviors

- **Copy-on-reference:** Copying a `Ref<T>` increments the reference count.
- **Auto-delete:** When the last `Ref<T>` is destroyed or reset, the managed object is `delete`d.
- **Implicit construction** from compatible raw pointers and derived-type refs.
- **Hash support:** `std::hash<Ref<T>>` is specialized for use in unordered containers.

### Ref Change Reporting

If a managed type defines `ReportRefChange()`, `ReportRefAdd()`, or `ReportRefRelease()` methods, the corresponding callback is invoked on each reference count change via compile-time detection (`DEFINE_HAS_FUNC`).

---

## Assertion Macros

**File:** `Core/Core.h`

Assertions are controlled by the `ENABLE_ASSERTS` macro. When enabled, failed assertions log an error message and trigger a debugger break (or control flow statement).

| Macro | Behavior on Failure |
|-------|-------------------|
| `CORE_ASSERT(x, msg, ...)` | Logs error + `__debugbreak()` |
| `CORE_ASSERT_BREAK(x, msg, ...)` | Logs error + `break` |
| `CORE_ASSERT_CONTINUE(x, msg, ...)` | Logs error + `continue` |
| `CORE_ASSERT_RETURN(x, msg, ...)` | Logs error + `return` |

When `ENABLE_ASSERTS` is not defined, all macros expand to nothing.

### Windows-Specific Assertions (from Win.h)

| Macro | Purpose |
|-------|---------|
| `CORE_ASSERT_HANDEL(x, ...)` | Asserts a Windows handle is valid (`!= INVALID_HANDLE_VALUE`) |
| `CORE_ASSERT_HRESULT(x, msg)` | Asserts an HRESULT succeeded; translates error to string on failure |

---

## Utility Macros

**File:** `Core/Core.h`

### Bit and Memory

```cpp
#define BIT(x)         (1 << (x))
#define MEM_KiB(kib)   ((kib) * 1024)
#define MEM_MiB(mib)   MEM_KiB((mib) * 1024)
#define MEM_GiB(gib)   MEM_MiB((gib) * 1024)
```

### Copy/Move Disable

```cpp
#define DISABLE_COPY(type)     type(const type&) = delete;
#define DISABLE_MOVE(type)     type(const type&&) = delete;
#define DISABLE_COPY_MOVE(type) DISABLE_COPY(type) DISABLE_MOVE(type)
```

### Token Pastes and Stringification

```cpp
#define TOKEN_PASTE(x, y)  x##y
#define CAT(x, y)          TOKEN_PASTE(x, y)
#define VAL(str)           #str
#define TO_STRING(str)     VAL(str)
```

### Common.h Macros

**File:** `Core/Common.h`

```cpp
namespace fs = std::filesystem;

#if defined(STRIP_CLANG)
    #define DISABLE_OPS
#else
    #define DISABLE_OPS __attribute__((optnone))
#endif
```

The `DEFINE_HAS_FUNC(F)` macro generates a compile-time trait `HasF<T>` and constexpr bool `HasF<T>` for detecting whether a type has a member function named `F`. Used internally by `Ref<T>` for ref-change reporting.

---

## String Formatting

**Files:** `Core/StringFormat.h`, `Core/StringFormat.cpp`

`Core::StringFormat` provides a positional formatting system using `std::stringstream` as the output sink.

### CRITICAL: Placeholder Syntax - NUMERIC INDEX REQUIRED

Placeholders **must** contain a zero-indexed number: `{0}`, `{1}`, `{2}`, etc. The number tells the formatter which variadic argument to substitute (by position, starting at 0).

**Bare `{}` without a number is INVALID and will NOT work.** It causes `ParseMessage` to call `std::from_chars` on an empty string, which returns `invalid_argument`, causing the entire format operation to abort. The message is output as-is with no substitution.

A backslash before `{` escapes it: `\{` prints a literal brace.

```cpp
using namespace Core;

// CORRECT - numbered placeholders match argument positions (0-indexed)
String msg = FormatString("Hello {0}, you have {1} messages.", "Alice", 42);
// Result: "Hello Alice, you have 42 messages."

// WRONG - bare {} is INVALID. The parser fails and NO substitution occurs.
String bad = FormatString("Hello {}, you have {} messages.", "Alice", 42);
// Result: the message is NOT formatted - it prints the raw string as-is
```

### Supported Types

The formatter auto-detects argument types at compile time:

| Category | Types |
|----------|-------|
| Unsigned integers | `uint8`, `uint16`, `uint32`, `uint64` |
| Signed integers | `int8`, `int16`, `int32`, `int64` |
| Floating point | `float` (FLOAT32), `double` (FLOAT64) |
| Characters | `char`, `char*`, `const char*`, char arrays |
| Strings | `std::string`, `Core::String`, `std::string_view` |
| Booleans | `bool` (printed as `"True"` / `"False"`) |

### API

```cpp
// Format to a stringstream
template<typename... Args>
static void Format(std::stringstream& ss, const char* msg, const Args&... args);

// Format and return Core::String
template<typename... Args>
static String Format(const char* msg, const Args&... args);

// Convenience function (same as StringFormat::Format)
template<typename... Args>
String FormatString(const char* msg, const Args&... args);
```

---

## Logging Hierarchy

**Files:** `Core/Logging/Log.h`, `Core/Logging/Log.cpp`

The logging system provides a two-logger architecture with configurable patterns and console color output.

### Log Levels

| Level | Value | Console Color (Windows) | Profiler severity |
|-------|-------|------------------------|-------------|
| `Trace` | 0 | White (15) | `ITP_TRACE` |
| `Info` | 1 | Green (10) | `ITP_INFO` |
| `Warn` | 2 | Yellow (14) | `ITP_WARNING` |
| `Error` | 3 | Red (12) | `ITP_ERROR` |

### Logger Configuration

Each `Logger` instance has its own output pattern and minimum log level.

**Pattern tokens:**

| Token | Output |
|-------|--------|
| `%t` | Current local time (`HH:MM:SS`) |
| `%m` | The formatted message |
| Other text | Printed literally |
| `%%` | Escaped percent sign |

### Two Logger Instances

The system defines two static loggers via the `IMPLEMENT_LOGGER` define:

| Logger | Pattern | Purpose |
|--------|---------|---------|
| **Core Logger** | `[%t] ENGINE: %m` | Engine internals |
| **Client Logger** | `[%t] APP: %m` | Application/game code |

### Logging Macros

```cpp
// Core (engine) logging - placeholders MUST be numbered: {0}, {1}, etc. NEVER use bare {}
CORE_TRACE("Loading asset {0}", assetName);
CORE_INFO("Frame started");
CORE_WARN("Low memory warning");
CORE_ERROR("Failed to open file {0}", path);

// Client (application) logging - same placeholder rules apply
DEBUG_TRACE("Custom trace message");
DEBUG_INFO("Player spawned at {0}", pos);
DEBUG_WARN("Deprecated API used");
DEBUG_ERROR("Shader compile failed: {0}", error);
```

> **CRITICAL:** Log messages use `Core::StringFormat` under the hood. Every placeholder **must** contain a number (e.g., `{0}`, `{1}`). Bare `{}` without an index is **INVALID** - it will cause the formatter to abort silently and output the raw message string with no substitution.

### Profiler Integration

When `ENABLE_PROFILING` is defined, each log line is also sent to the IED profiler as a message (`itp_message`, with the level's severity), so it shows on the capture's timeline.

---

## Performance and Profiling

- **Files:** `Profiling/Performance.h`, `Profiling/Performance.cpp`
- **Project:** `plugins/cpp/libraries/Profiling/`

> **Relocated from Core (card-1):** Performance profiling was moved from the Core library to the Profiling project. The new include path is `Profiling/Performance.h`. It is no longer transitively included by `Core/Core.h`.

The profiling system is a client of the IED Trace Protocol (`Profiling/Itp.h`, `Profiling/Itp.cpp`; IED `docs/16`). With `ENABLE_PROFILING` defined, a program run with `IED_PROFILER` set (IED's Profile task sets it to `127.0.0.1:<port>;token=<t>`; `file:<path>.iedtrace` writes a trace file) streams its zones, frames, messages, thread names and, with the memory tracker, allocations to the IED profiler; without `IED_PROFILER` each zone costs one relaxed load. Without `ENABLE_PROFILING` all macros expand to empty statements. `Itp.h` and `Itp.cpp` depend only on the C++ standard library and the system's sockets, so other C and C++ programs can copy them; their own API (`ITP_ZONE`, `itp_plot`, tasks, pools) is documented in `Itp.h`.

### Instrumentor Singleton

```cpp
namespace Profiler {
    class Instrumentor {
    public:
        static Instrumentor& Get();
        void RegisterThread(const std::string& name, uint32 order = 0);
        void RecordData(bool record);
    };
}
```

### Profiling Macros

| Macro | Description |
|-------|-------------|
| `FRAME_MARK` | Marks the end of a frame (`itp_frame`) |
| `CREATE_PROFILE_SCOPEI(name)` | Begins a named scoped zone; auto-ends at scope exit |
| `CREATE_PROFILE_FUNCTIONI()` | Profiles the current function using `__FUNCSIG__` as the name |
| `ANNOTATE_PROFILEI(annotation)` | Attaches a text annotation to the active scoped zone |

### Example

```cpp
void RenderFrame() {
    CREATE_PROFILE_FUNCTIONI();
    FRAME_MARK;

    // ... rendering code

    {
        CREATE_PROFILE_SCOPEI("UploadTextures");
        ANNOTATE_PROFILEI("Uploading 64 textures");
        // ... upload work
    } // zone auto-ends here
}
```

---

## Benchmarking Utilities

**File:** `Core/BenchmarkResult.h`

The benchmarking module provides structures for running, comparing, and tracking micro-benchmarks over time. It wraps `Core::Time` for high-resolution timing and computes statistical summaries from per-iteration samples.

### BenchmarkStats

Statistics computed from a single benchmark run's timing samples:

| Field | Type | Description |
|-------|------|-------------|
| `mean` | `double` | Arithmetic mean of all iteration times (seconds) |
| `median` | `double` | Median iteration time |
| `stddev` | `double` | Population standard deviation |
| `minTime` | `double` | Fastest single iteration |
| `maxTime` | `double` | Slowest single iteration |
| `iterations` | `uint32` | Number of timing samples |

```cpp
void ComputeFromSamples(const std::vector<double>& samples);
```

Computes all statistics from a collection of per-iteration timing values (seconds). Handles empty sample sets by zeroing all fields.

### BenchmarkResult

Pairs a test name with computed stats and total wall time:

| Field | Type | Description |
|-------|------|-------------|
| `name` | `String` | Benchmark identifier |
| `stats` | `BenchmarkStats` | Computed statistics |
| `totalWallTime` | `double` | Total elapsed wall-clock time for all iterations |

```cpp
static BenchmarkResult Run(const String& testName, const std::function<void()>& func, uint32 iterations);
```

Convenience factory: runs a function `iterations` times, collecting per-iteration timing samples, and returns a filled `BenchmarkResult`.

### BenchmarkComparison

Expresses relative performance change between two benchmark results:

| Field | Type | Description |
|-------|------|-------------|
| `nameA`, `nameB` | `String` | Names of the compared benchmarks |
| `meanA`, `meanB` | `double` | Mean iteration times for each |
| `ratio` | `double` | B/A ratio (>1 means B is slower, <1 means B is faster) |
| `percentChange` | `double` | Percentage change: ((B - A) / A) x 100 |

```cpp
static BenchmarkComparison Compare(const BenchmarkResult& resultA, const BenchmarkResult& resultB);
```

### HistoricalBenchmarkData

Tracks benchmark results over time for regression detection:

| Method | Return | Description |
|--------|--------|-------------|
| `AddResult(result)` | `void` | Append a new result to the history log |
| `GetBaselineMean()` | `double` | Earliest recorded mean (baseline) |
| `GetLatestMean()` | `double` | Most recent recorded mean |
| `RegressionPercent()` | `double` | Percentage change from baseline; 0.0 if no history |

### Stopwatch

A lightweight stopwatch wrapping `Core::Time::GetTime()`:

| Method | Return | Description |
|--------|--------|-------------|
| `Start()` | `void` | Start (or restart); accumulates existing elapsed time if already running |
| `Stop()` | `void` | Stop and record elapsed time |
| `Reset()` | `void` | Discard all state without recording |
| `GetElapsedSeconds()` | `double` | Elapsed seconds (updates live if still running) |
| `GetElapsedMilliseconds()` | `double` | Elapsed milliseconds |
| `IsRunning()` | `bool` | Whether the stopwatch is currently running |

### Example Usage

```cpp
using namespace Core;

// Run a benchmark
auto result = BenchmarkResult::Run("VectorDotProduct", []() {
    // ... work to measure
}, 10000);

CORE_INFO("Benchmark \"{0}\": mean={1}ms, median={2}ms, stddev={3}ms",
    result.name, result.stats.mean * 1000.0, result.stats.median * 1000.0,
    result.stats.stddev * 1000.0);

// Compare two runs
auto comparison = BenchmarkComparison::Compare(resultA, resultB);
if (comparison.percentChange > 5.0) {
    CORE_WARN("Regression detected: {0}% slower", comparison.percentChange);
}
```

---

## Time Utilities

**Files:** `Core/Time.h`, `Core/Time.cpp`

The `Core::Time` class provides frame timing based on `std::chrono::steady_clock`.

### API

| Method | Description |
|--------|-------------|
| `GetTime()` | Current time in seconds since epoch (double) |
| `UpdateDeltaTime()` | Must be called each frame to update delta time |
| `GetDeltaTime()` / `GetDeltaSeconds()` | Seconds elapsed since last `UpdateDeltaTime()` call (double) |
| `GetDeltaTimeF()` / `GetDeltaSecondsF()` | Same as above, returned as float |
| `GetDeltaMilliseconds()` / `GetDeltaMillisecondsF()` | Milliseconds since last update |
| `GetFPS()` | Frames per second based on current delta time |

### Usage

```cpp
// In your main loop:
Core::Time::UpdateDeltaTime();
float dt = Core::Time::GetDeltaTimeF();
float fps = Core::Time::GetFPS();
```

---

## UUID Generation

**Files:** `Core/UUID.h`, `Core/UUID.cpp`

`Core::UUID` is a 64-bit unique identifier using `std::mt19937_64` seeded by `std::random_device`.

### API

| Method | Description |
|--------|-------------|
| `UUID()` | Generate a random UUID |
| `UUID(uint64)` | Construct from a specific value |
| `static Create()` | Factory: generate random UUID |
| `static Create(uint64)` | Factory: construct from value |
| `operator uint64()` | Implicit conversion to raw value |

### Windows-Specific

On Windows, UUID implicitly converts to `LUID` for use with Win32 APIs.

### Hash Support

`std::hash<Core::UUID>` is specialized, enabling use as a key in `std::unordered_map` and `std::unordered_set`.

---

## Memory Tracker

- **Files:** `Profiling/MemTracker/MemTracker.h`, `Profiling/MemTracker/MemTracker.cpp`
- **Project:** `plugins/cpp/libraries/Profiling/`

> **Relocated from Core (card-1):** MemTracker was moved from the Core library to the Profiling project on 2026-07-12. The new include path is `Profiling/MemTracker/MemTracker.h`. Downstream callers that depend on memory tracking must include this header explicitly; it is no longer transitively available via `Core/Core.h`.

The memory tracker overrides global `new`/`delete` to record allocation statistics. It is controlled by the `ENABLE_MEMORY_TRACKER` define (default: `0`).

### MemStats Structure

```cpp
struct MemStats {
    std::atomic<uint64> totalAllocations = 0;   // Cumulative allocation count
    std::atomic<uint64> totalFrees = 0;          // Cumulative free count
    std::atomic<uint64> numAllocations = 0;      // Current live allocations
    std::atomic<uint64> allocatedBytes = 0;      // Current live bytes

    static constexpr bool IsActive;              // true when ENABLE_MEMORY_TRACKER
    static MemStats s_MemStats;                  // Global instance
};
```

### Accessing Stats

```cpp
auto& stats = Core::GetMemStats();
uint64 currentAllocs = stats.numAllocations.load();
uint64 currentBytes  = stats.allocatedBytes.load();
```

### Implementation Details

- Overrides `operator new`, `operator new[]`, `operator delete`, `operator delete[]`
- Uses `malloc`/`free` internally (to avoid recursive calls)
- Tracks allocation size via `_msize()` on free (Windows-specific)
- Reports every allocation and free to the IED profiler (`itp_alloc`/`itp_free`, pool 0) when `ENABLE_PROFILING` is set
- Throws `std::bad_alloc` when `malloc` fails, as a replaced `operator new` must
- Guards against double-free by checking that reported size does not exceed tracked bytes

---

## Threading Primitives

- **Files:** `Threading/Flag.h`, `Threading/Flag.cpp`, `Threading/JobSystem.h`, `Threading/JobSystem.cpp`
- **Project:** `plugins/cpp/libraries/Threading/`

\u003e **Relocated from Core (card-2):** Threading primitives were moved from the Core library to the standalone Threading project on 2026-07-12. Include via `Threading/Flag.h` and `Threading/JobSystem.h`; they are no longer available transitively through `Core/Core.h`.

### Flag

A thread-safe boolean flag backed by `std::mutex` and `std::condition_variable`. Used for signaling between threads.

| Method | Description |
|--------|-------------|
| `Signal()` | Set flag to `true` and notify waiters |
| `Clear()` | Set flag to `false` |
| `Wait(waitVal)` | Block until flag equals `waitVal` |
| `WaitAndSignal()` | Wait for `false`, then signal (handoff pattern) |
| `GetFlag()` | Read current value |

### JobSystem

**Files:** `Threading/JobSystem.h`, `Threading/JobSystem.cpp`

A named job thread system for running background tasks on dedicated threads.

#### NamedJobThread

Each named job is a persistent OS thread that waits for signals and executes an assigned function:

| Method | Description |
|--------|-------------|
| `SetData(void*)` | Attach data pointer to the next invocation |
| `Invoke()` | Signal the thread to run its function |
| `Invoke(void* data)` | Set data, then signal |
| `Wait()` | Block until the thread finishes (flag is `false`) |
| `IsExecuting()` | Check if the thread is currently running |
| `SetFunc(JobFunc)` | Assign the callback function |

#### JobSystem Registry

```cpp
// Register a named job thread
auto* job = Threading::JobSystem::AddNamedJob("AssetLoader", MyLoadFunction);

// Get and invoke
job->Invoke(pData);

// Lookup by name
auto* found = Threading::JobSystem::GetNamedJob("AssetLoader");

// Convenience helpers
Threading::JobSystem::InvokeNamedThread("AssetLoader");
Threading::JobSystem::WaitForNamedThread("AssetLoader");
```

---

## Platform Abstraction

### PlatformCommon.h

**File:** `Core/Platform/PlatformCommon.h`

Cross-platform utility functions in the `Platform` namespace:

| Function | Description |
|----------|-------------|
| `GetApplicationPath()` | Full path to the running executable |
| `GetApplicationFolder()` | Directory containing the executable |
| `LoadDynamicLibrary(wchar_t* path)` | Load a DLL/shared library; returns opaque handle |
| `GetLibraryExport(void* h, const char* name)` | Get exported symbol address from loaded library |
| `SystemCall(cmd, dir, supress)` | Execute a shell command in the given directory |
| `ClipboardHasText()` | Check if clipboard contains text |
| `ClipboardGetText()` | Get clipboard text content |
| `ClipboardSetText(text)` | Set clipboard text content |

### Windows Platform (Win.h)

**File:** `Core/Platform/Windows/Win.h`

The Windows platform header:

- Targets **Windows 10+** (`_WIN32_WINNT 0x0A00`)
- Uses `WIN32_LEAN_AND_MEAN` and `NOMINMAX` to reduce macro pollution
- Defines numerous `NO*` macros to exclude unused Win32 headers (unless `FULL_WINTARD` is defined)
- Includes `<Windows.h>` and `<comdef.h>`

#### Windows-Specific Assertions

| Macro | Description |
|-------|-------------|
| `CORE_ASSERT_HANDEL(x, ...)` | Assert handle is not `INVALID_HANDLE_VALUE` |
| `CORE_ASSERT_HRESULT(x, msg)` | Assert HRESULT succeeded; translates error via `TranslateError()` |

#### TranslateError

```cpp
std::wstring TranslateError(HRESULT hr);
```

Converts an HRESULT to a human-readable wide string using COM error information.

---

## Bit Flags

**File:** `Core/BitFlags.h`

A simple 32-bit flag container:

```cpp
Core::BitFlags flags;
flags.SetFlag(MY_FLAG);
flags.ClearFlag(MY_FLAG);
flags.SetFlag(MY_FLAG, true);   // Conditional set
bool has = flags.TestFlag(MY_FLAG);
```

---

## Dynamic Bit Allocator

**File:** `Core/DynamicBitAllocator.h`

A thread-safe, growable bit allocator for assigning unique indices. Uses pages of 64 bits (`uint64`) and grows as needed.

### API

| Method | Description |
|--------|-------------|
| `AllocateIndex()` | Returns a unique allocated index (thread-safe) |
| `Free(index)` | Release an allocated index |
| `HasAllocation(index)` | Check if an index is currently allocated |
| `GetNumPages()` | Number of 64-bit pages in use |
| `Clear()` | Free all allocations (reset all bits to zero) |

### Allocation Strategy

Scans existing pages for the first free bit using `std::countr_one` (count trailing ones). If no page has a free slot, allocates a new page. Protected by `std::mutex`.

---

## Hash - Compile-Time CRC32

**File:** `Core/Hash.h`

Provides a compile-time CRC32 hash via template metaprogramming. Uses a 256-entry lookup table and recursive template instantiation.

### Usage

```cpp
// Compute CRC32 at compile time (null terminator excluded)
constexpr uint32 hash = COMPILE_TIME_CRC32_STR("MyString");
```

The `Core::MM<size, idx>` struct recursively processes each byte of the string at compile time, producing a constant CRC32 value. Useful for string-based enums and resource identifiers.

---

## Argument List

**Files:** `Core/ArgList.h`, `Core/ArgList.cpp`

Parses command-line arguments into named arguments (`-name=value`) and flags (no dash prefix).

### API

| Method | Description |
|--------|-------------|
| `ArgList(Vector<String>& args)` | Parse a vector of argument strings |
| `GetArg("-name")` | Get argument by name; returns pointer to `Arg{ name, value }` or `nullptr` |
| `HasFlag("flag")` | Check if a positional flag is present (flags do not start with `-`) |
| `s_GlobalArgs` | Static global instance for application-wide access |

### Format Normalization

Arguments starting with `/`, `\`, or `--` are normalized to `-name` form during parsing.

---

## UTF-8 and Encoding Utilities

**Files:** `Core/Utils.h`, `Core/Utils.cpp`

### Narrow/Wide String Conversion

| Function | Description |
|----------|-------------|
| `GetWStr(const char*)` | Convert narrow string to wide (`wchar_t*`) |
| `GetStr(const wchar_t*)` | Convert wide string to narrow (`char*`) |
| `GetWStr(const std::string&)` | Inline: `std::string` -> `std::wstring` |
| `GetStr(const std::wstring&)` | Inline: `std::wstring` -> `std::string` |

### UTF-8 Helpers

| Function | Description |
|----------|-------------|
| `utf8charlen(const char*)` | Byte length of the current UTF-8 character (1-4) |
| `utf8len(const char*)` | Total character count in a UTF-8 string |
| `utf8valid(const char*)` | Validate that a string is well-formed UTF-8 |
| `GetNextChar32(...)` | Extract the next UTF-32 code point from a UTF-8 string (multiple overloads) |

---

## Math Module

**Files:** `src/Math/` (included via `Core/Core.h`)

The math module provides custom engine-specific vector, matrix, and quaternion types with optimized SIMD routines (SSE4.1/AVX2) and scalar fallbacks.

### Constants

| Constant | Value |
|----------|-------|
| `Math::PI` | 3.141592653589793f |
| `Math::TAU` | PI x 2 |
| `Math::E` | 2.718281828459045f |
| `Math::EPSILON` | 1e-10 |

### Function Categories

All math functions are templated on type `T` and implemented natively:

- **Trigonometric:** `Sin`, `Cos`, `Tan`, `Cot`, `Sec`, `Csc`, `Asin`, `Acos`, `Atan`, `Acot`, `Asec`, `Acsc`, `Atan2`
- **Hyperbolic:** `Sinh`, `Cosh`, `Tanh`
- **Angle conversion:** `Degrees`, `Radians`
- **Arithmetic:** `Sqrt`, `InvSqrt`, `Pow`, `Exp`, `Log`, `Floor`, `Ceil`, `Abs`, `Clamp`, `Min`, `Max`, `Mod`
- **Interpolation:** `Lerp`, `Slerp`
- **Noise:** `Perlin` (1D-4D, with optional repetition), `Simplex` (1D-4D)

### Types

The math module defines custom vector (`Vector2`, `Vector3`, `Vector4`), matrix, and quaternion types with native implementations. These are **not** built on GLM - they are hand-written C++20 types with optimized SIMD routines (SSE4.1/AVX2) and scalar fallbacks.

---

## Build Configuration

**File:** `plugins/cpp/libraries/Core/nuke.toml`

| Key | Value |
|-----|-------|
| **kind** | `library` (a static library, `Core.lib`) |
| **group** | `C++ Libraries` |
| **depends** | `yaml` (yaml-cpp, `plugins/cpp/libraries/yaml`) |
| **Reflection** | none |

A project that depends on `Core` compiles with its include folder and links `Core.lib` and yaml-cpp's
(Nuke/README.md section 9c). It is built by Nuke's C/C++ driver like every other C++ project.
