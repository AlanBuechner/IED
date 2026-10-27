---
name: c4-language
description: How to write C4 - complete small programs to copy (a command-line tool, a header and its source, a class, tests), the C++ habits that do not compile and what to write instead, the everyday Bedrock API with exact names (String, StringView, StringBuilder, Format, Array, HashMap, HashSet, sorting, files, console), the program model, then the language reference - types, [operator] constructors and operators, extension methods, lambdas, interfaces, templates, packed SIMD vectors, builtins.
triggers:
  - "writing C4 code"
  - "C4 syntax"
  - "new C4 program main argc argv"
  - "Bedrock String Array HashMap API"
  - "C++ to C4 differences"
  - ".cf .ch files include program model"
  - "operator create delete annotation"
  - "class struct interface template lambda"
---

# Writing C4

C4 compiles like C++ and looks like C++, but its syntax for types, constructors and operators differs, it has no
standard C++ library, and every file of a project is one program. Write C4 by the examples here, not from C++ memory.
When a name is not on this page, look it up in the API digest rather than guessing -
`docs_read({"path": "c4/library/bedrock-api.md", "heading": "Core::HashMap"})` gives a Bedrock type's members with their
signatures, `docs_search({"query": "ParseInt64"})` finds a function.

## 1. A program to start from

A Nuke workspace: `nuke.toml`, sources in the folders a project's `sources` names - `src/` when it names none (`.cf` definitions, `.ch` declarations) - and tests in a `tests/` folder inside one of them. A project's code is not always in `src/`: read its `nuke.toml` (or `project_list`, which gives each project's folder) before assuming where it is. The examples below use the default layout.
`nuke build`, `nuke test`, `nuke run -- <args>` (or build_run with task build, test, run).

```c4
// src/main.cf - a command-line tool: arguments, a file, standard output and error, exit codes
include "bedrock.ch"
include "stations.ch"

const int32 kExitUsage = 2;
const int32 kExitError = 1;

int32 main(int32 argc, char** argv) {
	if (argc != 2) {
		Platform::WriteError(Core::StringView("usage: stations <readings.csv>\n"));
		return kExitUsage;
	}
	FS::File file = FS::File(argv[1], "rb");
	if (!file.IsOpen()) {
		Platform::WriteError((Core::String("error: cannot read ") + argv[1] + "\n").View());
		return kExitError;
	}
	Core::String text = file.ReadText();
	Core::String problem;
	Core::HashMap<Core::String, Stations::Summary> byStation;
	if (!Stations::Read(text.View(), byStation, problem)) {
		Platform::WriteError((problem + "\n").View());
		return kExitError;
	}
	Core::StringBuilder out;
	for (const Stations::Row& row : Stations::Sorted(byStation))
		Core::Format(&out, "{0}: min {1:F.1} mean {2:F.1} max {3:F.1} ({4} readings)\n", row.name, row.summary.low, row.summary.Mean(), row.summary.high, row.summary.count);
	Platform::WriteOutput(out.View());
	return 0;
}
```

```c4
// src/stations.ch - declarations others include
include "bedrock.ch"

namespace Stations
{
	struct Summary
	{
		public double low;
		public double high;
		public double total;
		public int64 count;

		public void Add(double value);
		public double Mean() const { return count > 0 ? total / (double)count : 0.0; }
	}

	struct Row
	{
		public Core::String name;
		public Summary summary;
	}

	// "station,temperature" lines into one summary per station; false, with `problem` saying where, on a bad line.
	bool Read(Core::StringView text, Core::HashMap<Core::String, Summary>& byStation, Core::String& problem);
	Core::Array<Row> Sorted(const Core::HashMap<Core::String, Summary>& byStation);
}
```

```c4
// src/stations.cf - the definitions
include "stations.ch"

namespace Stations
{
	public void Summary::Add(double value) {
		if (count == 0 || value < low) low = value;
		if (count == 0 || value > high) high = value;
		total += value;
		count += 1;
	}

	bool Read(Core::StringView text, Core::HashMap<Core::String, Summary>& byStation, Core::String& problem) {
		int64 number = 0;
		for (Core::StringView line : text.Split('\n')) {
			number += 1;
			line = line.Trimmed();
			if (line.IsEmpty()) continue;
			Core::Array<Core::StringView> fields = line.Split(',');
			double value = 0.0;
			if (fields.Count() != 2 || !Core::ParseDouble(fields[1].Trimmed(), value)) {
				Core::StringBuilder said;
				Core::Format(&said, "line {0}: expected station,temperature", number);
				problem = said.ToString();
				return false;
			}
			Summary* summary = byStation.FindAs(fields[0]);   // looked up by view: no String made
			if (!summary) summary = &byStation[Core::String(fields[0])];
			summary->Add(value);
		}
		return true;
	}

	Core::Array<Row> Sorted(const Core::HashMap<Core::String, Summary>& byStation) {
		Core::Array<Row> rows;
		for (const Core::KeyValue<Core::String, Summary>& entry : byStation) rows.Push(Row{ entry.key, entry.value });
		rows.SortBy([](const Row& a, const Row& b) -> bool { return a.name < b.name; });
		return rows;
	}
}
```

```c4
// src/histogram.ch - a class: members say their visibility one by one; methods defined out of line repeat it
include "bedrock.ch"

namespace Stations
{
	class Histogram
	{
		[operator("create")] public void Init(double low, double width, int64 buckets) { start = low; step = width; counts.Resize(buckets); }
		public void Add(double value);
		public int64 CountAt(int64 bucket) const { return counts[bucket]; }
		public int64 Outside() const { return outside; }

		private Core::Array<int64> counts;
		private double start;
		private double step;
		private int64 outside;
	}
}

// src/histogram.cf
include "histogram.ch"

namespace Stations
{
	public void Histogram::Add(double value) {
		const int64 bucket = (int64)floor((value - start) / step);
		if (bucket < 0 || bucket >= counts.Count()) outside += 1;
		else counts[bucket] += 1;
	}
}
```

```c4
// src/tests/stations_tests.cf - [test] functions; `nuke test` (or build_run task "test") runs them
include "stations.ch"
include "histogram.ch"

[test("stations.read.summarizes_each_station")]
static void SummarizesEachStation() {
	Core::HashMap<Core::String, Stations::Summary> byStation;
	Core::String problem;
	assert_true(Stations::Read(Core::StringView("oslo,-3.5\nlima,20\noslo,1.5\n"), byStation, problem), "reads");
	assert_eq((int64)2, byStation.Count(), "two stations");                  // (expected, actual, message), compared as int64
	Core::Array<Stations::Row> rows = Stations::Sorted(byStation);
	assert_se("lima", rows[0].name.CStr(), "sorted by name");                // C strings
	assert_true(rows[1].summary.low == -3.5 && rows[1].summary.Mean() == -1.0, "oslo's low and mean");
}

[test("stations.read.names_a_bad_line")]
static void NamesABadLine() {
	Core::HashMap<Core::String, Stations::Summary> byStation;
	Core::String problem;
	assert_true(!Stations::Read(Core::StringView("oslo,1\noslo,warm\n"), byStation, problem), "fails");
	assert_se("line 2: expected station,temperature", problem.CStr(), "says where");
}

[test("stations.histogram.buckets")]
static void Buckets() {
	Stations::Histogram histogram = Stations::Histogram(0.0, 10.0, 3);
	histogram.Add(5.0);
	histogram.Add(25.0);
	histogram.Add(31.0);
	assert_eq((int64)1, histogram.CountAt(0), "0 to 10");
	assert_eq((int64)1, histogram.Outside(), "past the last bucket");
}
```

## 2. C++ habits that do not compile

| C++ / Go habit | C4 |
|---|---|
| `#include <vector>`, `using namespace std`, `std::string` | `include "bedrock.ch"`; `Core::String`, `Core::Array<T>`, `Core::HashMap<K, V>`; no `std` |
| `auto x = f();` | no `auto`: write the type (`int64 x = f();`) |
| `int a[4];`, `char buf[64];`, `int* p, q;` | `int32[4] a;` - extents and pointers belong to the type; `Core::StringBuilder` instead of char buffers |
| `int`, `size_t`, `long` for sizes | `int64` (every Bedrock count and index); `int` is `int32` |
| `Point(int x) : x(x) {}`, `~Point()` | `[operator("create")] public void Init(int32 px) { x = px; }`, `[operator("delete")] public void Destroy()` |
| `bool operator==(const P& o) const` | `[operator("==")] public bool Equals(const P& o) const` (also `"<"`, `"+"`, `"[]"` is `"index"`, `"hash"`) |
| `class C { int x; public: int y; };` | `class C { private int32 x; public int32 y; }` - every member says its own visibility; a `public:` label only requires it |
| `std::string s = "hi";` | `Core::String s = Core::String("hi");` (comparing works directly: `s == "hi"`) |
| `printf("%s", s)` with a String | printf is C's: pass `s.CStr()` (a String through printf's `...` does not compile; a view: `printf("%.*s", (int32)v.ByteLength(), v.Data())`); `Core::Format(&b, "{0}", s)` takes the String itself |
| `v.push_back(x)`, `v.size()`, `v.empty()`, `v.back()` | `v.Push(x)`, `v.Count()`, `v.IsEmpty()`, `v.Back()` |
| `m.find(k) != m.end()`, `m.count(k)`, `m.insert({k, v})` | `m.Find(k)` (a `V*`, null when absent), `m.Contains(k)`, `m.Insert(k, v)`; `m[k]` creates a zero value |
| `std::sort(v.begin(), v.end(), cmp)` | `v.Sort()` (by `<`) or `v.SortBy([](const T& a, const T& b) -> bool { return ...; })` |
| `[](int x) { return x * 2; }` returning a value | `[](int32 x) -> int32 { return x * 2; }` - a lambda that returns names its type |
| `std::stoi`, `atoi`, `std::stod`, `std::to_string` | `Core::ParseInt64(view, value)`, `Core::ParseDouble(view, value)` (false when not a number), `builder.AppendInt(n)` |
| `std::cin`, `getline` | `Platform::InputLines lines; Core::String line; while (lines.Next(line)) { ... }` (no line break in `line`), or `Platform::ReadAllInput(builder)` |
| `T& r = c ? a : b;` to pick one of two objects | `?:` on objects makes a copy: pick through pointers, `T* r = c ? &a : &b;` |
| `p->x` on an object, `o.x` through a pointer | `.` on objects and references, `->` through pointers only |
| a helper, `const` or variable with the same name in two `.cf` files | one program: the second is a redefinition unless both are `static` (private to their file); give a shared name one home (a header) |

## 3. Bedrock every day

`include "bedrock.ch"` brings in Bedrock (not math: `include "math.ch"`). Counts and indices are `int64`; element
access is unchecked; a zero value of any container or string is empty. Full reference: the toolchain's
`docs/library/bedrock.md`; every type's and function's signatures: `docs/library/bedrock-api.md` (docs_read).

| Type | Everyday members |
|---|---|
| `Core::StringView` (a view; pass by value) | `StringView("lit")`, `Data()`, `ByteLength()`, `IsEmpty()`, `ByteAt(i)`, `Substring(start, length)` (clamped, no copy), `IndexOf(char)` / `IndexOf(char, from)` (-1 when absent), `IndexOf(view, from)`, `Contains`, `StartsWith`, `EndsWith`, `Trimmed()`, `Split(',')` (an Array of views into it), `ToLower()`, `==`, `<` |
| `Core::String` (owns UTF-8) | `String("lit")`, `String(view)`, `View()`, `CStr()`, `ByteLength()`, `IsEmpty()`, `Split`, `ToLower()`, `+` (String on the left; with String, view or literal: a new String), `+=`, `Clear()`, `==` / `!=` with a String, view or literal on the right (a view on the left: `view == s.View()`), `<` with another String, `hash` (a HashMap key). Substring, Trimmed, IndexOf, StartsWith, Contains: through `s.View()` |
| `Core::StringBuilder` | `Append(view or literal)` (a String: `Append(s.View())`), `AppendByte(c)`, `AppendInt(int64)`, `AppendDouble(d, decimals)`, `AppendIntBase(n, base, minDigits)`, `View()`, `CStr()`, `ToString()`, `Clear()`, `ByteLength()` |
| `Core::Format(&builder, "{0} {1} {2:F.2} {3:x}", count, name, ratio, mask)` | appends; `{n}` writes any integer, float or double (its shortest text), String, StringView, const char*, bool or char as it is; `{n:F.d}` / `{n:f.d}` d fraction digits (`{n:F}` alone: 6, as `%f`), `{n:x}` / `{n:X}` hex, `{n:d.N}` pads an integer to N digits, `{{` a brace. Only into a builder: `&builder`, never a String. A type Format cannot write is a compile error |
| `Core::Array<T>` | `= { 1, 2 }`, `Array<T>(capacity)`, `Push(v)`, `Pop()`, `Count()`, `IsEmpty()`, `[i]`, `Front()`, `Back()`, `Insert(i, v)`, `RemoveAt(i)`, `RemoveRange(first, end)`, `Clear()`, `Resize(n)`, `Reserve(n)`, `Sort()`, `SortBy(before)`, `IndexOf(v)`, `Contains(v)`, `LowerBound(v)`, `LowerBoundBy(v, before)`; range-for |
| `Core::HashMap<K, V>` | `Insert(k, v)` (true when added), `m[k]` (`V&`, created when absent), `Find(k)` (`V*` or nullptr), `FindAs(view)` or `FindAs("lit")` for String keys, `Get(k)` (a copy, zero when absent), `TryGet(k, out)`, `Contains(k)`, `Remove(k)`, `Count()`; range-for gives `Core::KeyValue<K, V>&` (`.key`, `.value`) in no particular order |
| `Core::HashSet<T>` | `Add(v)` (true when new), `Contains(v)`, `ContainsAs(view)`, `Remove(v)`, `Count()`; range-for |
| `Core::Queue<T>`, `Core::Stack<T>`, `Core::PriorityQueue<T>`, `Core::List<T>`, `Core::LruCache<K, V>` | `Push`, `Pop`, `Front` / `Top`, `Count`, `IsEmpty` |
| `Core::ParseInt64(view, value)` | decimal text, optional `-`, nothing around it; false (value unchanged) otherwise |
| `FS::File` | `FS::File(path, "rb")` (modes as fopen), `IsOpen()`, `ReadText()` (the rest as a String), `Read`, `Write(bytes, count)`, `Close()`; `FS::FileExists(path)`, `FS::WriteFileAtomically(path, bytes, count)`, `FS::Path`, `FS::Directory(path)` (range-for over entries) |
| `Platform::WriteOutput(view)`, `Platform::WriteError(view)` | standard output and error (printf also writes to standard output) |
| `Serialization::ReadJson(text, doc)`, `WriteJson(node, &builder, indent)`, `ToJson(value, indent)` / `FromJson(text, value)` | JSON as a DOM or into reflected structs |
| `Threading::Thread::Run(lambda)`, `Mutex`, `LockGuard`, `Atomic<T>`, `JobSystem` | threads and jobs |

## 4. The program model

All files of a project compile together as one program (no per-file object and link step):
- `include "file.ch"` names a file beside the includer or in the project's source folders; never `../`. It is not
  the C preprocessor: every includer sees the same header, so a `#define` before an include does not change it.
  Macros (`#define`) do carry from an included header to the includer.
- Outside function bodies, order does not matter: no forward declarations of types are needed. Functions defined in
  a `.cf` are declared in the `.ch` others include.
- A file sees another file's functions, types and constants only through a `.ch` it includes. The names still share
  the program: two files may not define the same function, type or `const` unless both are `static` (private to their
  file).
- A `static` variable in a `.ch` is defined there (the header is a source file); templates and their methods may be
  defined in a `.cf`.

## 5. Types and declarations

Primitives: `void`, `bool`, `int8/16/32/64`, `uint8/16/32/64`, `float`, `double`, `char8` (byte), `char16`, `char32`,
normalized `snorm*`/`unorm*`; aliases `int` = `int32`, `uint` = `uint32`, `char` = `char8`. Objects: `class`,
`struct` (public by default), `interface`; objects are values unless made with `new`.

```c4
const int32[10] a;            // array of 10 const ints
const int32* p;               // pointer to const int
int32 const* q;               // const pointer to a mutable int (`int32* const q` is an error)
int32& r = x;                 // reference
struct View { int32& r; }     // a reference field, bound when the object is made
```

Memory is read only as its own type (strict aliasing); reinterpret numbers with `reinterpret_cast<uint32>(f)` or a
union. Every type is zero-initialized; a field may have an initializer (`public int32 count = 3;`). Unions are
declared inside an object; anonymous ones expose their fields. `enum class Color { Red, Green }` (qualified:
`Color::Red`) and plain `enum`. Constants that fold (`const int64 kLimit = 64;`) work in array extents and case
labels. Write `0.5f`, never `.5f`.

## 6. Objects: construction, operators, inheritance

```c4
class Counter
{
	[operator("create")] public void Init() { value = 1; }                 // default constructor
	[operator("create")] public void Init(int32 start) { value = start; }   // Counter c(5); Counter d = Counter(5);
	[operator("copy")] public void CopyFrom(const Counter& o) { value = o.value; }
	[operator("delete")] public void Destroy() { }                          // runs at scope exit, delete, or collection
	[operator("+")] public Counter Add(const Counter& o) const { return Counter(value + o.value); }
	[operator("==")] public bool Equals(const Counter& o) const { return value == o.value; }
	[operator("index")] public int32 At(int64 i) const { return value; }
	public int32 value;
}
```

- Operators are methods of the left operand's type (`2.0f * v` needs an extension method of `float`). Token or name:
  `"+"` / `"add"`, `"+="` / `"add_assign"`, `"-"` (one parameter: subtract; none: negate), `"=="`, `"<"`,
  `"index"` / `"index_assign"`, `"call"`, `"hash"`, `"begin"` / `"end"` for range-for, `"cast"`. Lifecycle:
  `"create"`, `"copy"`, `"delete"` (the only destructor; `"destroy"` is an error). No move operator: objects relocate;
  `take(x)` moves out of a variable. Construction zeroes, builds the base (a base create called as the first statement:
  `Base::Init(x);`), the members, then runs the body; creates are not inherited.
- Heap: `T* p = new T(args);` (collected by the GC; `delete p;` runs the destructor now), `new T[n]`.
- Single inheritance plus interfaces: `class Square : Shape, IArea { ... }`; `interface IArea { int32 Area(); }`
  (methods public and virtual); override with `virtual` / `override`; `Base::F()` calls without dispatch;
  `p as Dog*` / `dynamic_cast<Dog*>(p)` (null when not one), `p is Dog`. `final class X`. No `friend`.
- Visibility: `private` (the type's methods and nested types), `protected`, `public` on each member; a `public:` label
  requires every member in its scope to say `public`.
- Static members: `public static int32 Count;`, used as `Type.Count` or `Type::Count`; static methods only as `Type::F()`.
- Extension methods add members to any type: `float Length() extends Point;` (declare in a header, define with the
  same line and a body); `this` is implicit. Operators outside a type body must be extensions.

## 7. Functions, lambdas, templates

```c4
funcptr<int32(int32)> f = [](int32 x) -> int32 { return x * 10; };   // capture-less lambda as a function pointer
int32 base = 5;
func<int32(int32)> add = [base](int32 x) -> int32 { return x + base; };   // func<> holds any callable
func<int32()> counter = [n = 0]() mutable -> int32 { return ++n; };
template<T> T Max(T a, T b) { return a < b ? b : a; }
template<K, V> class Pair { public K key; public V value; }
template<F> void Each(const Core::Array<int32>& items, F f) { for (int32 v : items) f(v); }
```

Captures as C++: `[x]`, `[&x]`, `[=]`, `[&]`, `[n = expr]`, `[this]`. Templates take types (`template<T>`), values,
packs (`template<...Ts>`); a member of a template instance compiles only when used. Default arguments work: a function's
default may be on its `.ch` declaration or its `.cf` definition (both: the same expression) and applies to every call.

## 8. Statements

C++ control flow plus: `defer stmt;` (runs at scope exit, also on return or exception), `try / catch (T e) /
finally`, `fallthrough;` in a switch, range-for (`for (int32 v : a)` copies, `for (int32& v : a)` writes,
`for (const T& v : a)` reads without a copy). A condition is a bool, number, enum or pointer. A non-void function
returns on every path.

## 9. Builtins (no include)

`printf` (and `fflush`), `memcpy`, `memset`, `memcmp`, `strlen`, `strcmp`; C math (`sqrt`, `pow`, `floor`, ...,
`sqrtf` for float); `sizeof(T)`; casts `(int32)x`, `reinterpret_cast<T>(x)`; `assert_true`, `assert_eq`,
`assert_feq`, `assert_se` (tests); `take(x)`; variadics through `va_list`. GC and reflection builtins exist too
(c4-garbage-collector, c4-reflection skills).

## 10. Packed SIMD vectors

`packed float[8] v = 1.0f;` is an 8-lane vector value: `+ - * /` lane by lane, comparisons give `packed bool[8]`
masks, `simd_select`, `simd_min`, `simd_reduce_add`, `simd_load(p, 8)`, `simd_store`. Named lanes in a union make
swizzling types: `class Vec3 { public union { packed float x, y, z; }; }` with `v.xy = { 1.0f, 2.0f }`,
`v.zx = w.xy`, `v.xyz *= 2.0f`. `include "math.ch"` has `Math::FVector3`, `Mat4`, `Quat` and 8-wide batches. Details:
the toolchain's `docs/language/11-vectors-and-swizzling.md` and `14-builtins.md` section 11.

## 11. Conventions

One concept per file, named after its type or feature; declarations in `.ch`, definitions in the paired `.cf`; tabs;
minimal comments (a short line saying why).
