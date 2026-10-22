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

A Nuke workspace: `nuke.toml`, sources in `src/` (`.cf` definitions, `.ch` declarations), tests in `src/tests/`.
`nuke build`, `nuke test`, `nuke run -- <args>` (or build_run with task build, test, run).

```c4
// src/main.cf - a command-line tool: arguments, a file, standard output and error, exit codes
include "bedrock.ch"
include "wordcount.ch"

const int32 kExitUsage = 2;
const int32 kExitError = 1;

int32 main(int32 argc, char** argv) {
	if (argc != 2) {
		Platform::WriteError(Core::StringView("usage: words <file>\n"));
		return kExitUsage;
	}
	FS::File file = FS::File(argv[1], "rb");
	if (!file.IsOpen()) {
		Platform::WriteError((Core::String("error: cannot read ") + argv[1] + "\n").View());
		return kExitError;
	}
	Core::String text = file.ReadText();
	Core::StringBuilder out;
	for (const Words::WordCount& each : Words::Top(Words::Count(text.View()), 10)) {
		out.Append(each.word.View());
		out.Append(" ");
		out.AppendInt(each.count);
		out.Append("\n");
	}
	Platform::WriteOutput(out.View());
	return 0;
}
```

```c4
// src/wordcount.ch - declarations others include
include "bedrock.ch"

namespace Words
{
	struct WordCount
	{
		public Core::String word;
		public int64 count;
	}

	Core::HashMap<Core::String, int64> Count(Core::StringView text);
	Core::Array<WordCount> Top(const Core::HashMap<Core::String, int64>& counts, int64 limit);
}
```

```c4
// src/wordcount.cf - the definitions
include "wordcount.ch"

namespace Words
{
	static bool IsLetter(int32 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

	Core::HashMap<Core::String, int64> Count(Core::StringView text) {
		Core::HashMap<Core::String, int64> counts;
		int64 start = -1;
		for (int64 i = 0; i <= text.ByteLength(); ++i) {
			const bool letter = i < text.ByteLength() && IsLetter(text.ByteAt(i));
			if (letter && start < 0) start = i;
			if (letter || start < 0) continue;
			Core::StringView word = text.Substring(start, i - start);
			int64* seen = counts.FindAs(word);            // looked up by view: no String made
			if (seen) *seen += 1;
			else counts.Insert(Core::String(word), 1);
			start = -1;
		}
		return counts;
	}

	Core::Array<WordCount> Top(const Core::HashMap<Core::String, int64>& counts, int64 limit) {
		Core::Array<WordCount> ranked;
		for (const Core::KeyValue<Core::String, int64>& entry : counts) ranked.Push(WordCount{ entry.key, entry.value });
		ranked.SortBy([](const WordCount& a, const WordCount& b) -> bool { return a.count != b.count ? a.count > b.count : a.word < b.word; });
		if (ranked.Count() > limit) ranked.Resize(limit);
		return ranked;
	}
}
```

```c4
// src/spanset.ch - a class: members say their visibility one by one; methods defined out of line repeat it
include "bedrock.ch"

namespace Spans
{
	class SpanSet
	{
		[operator("create")] public void Init(int64 capacity) { spans.Reserve(capacity); }
		public void Add(int64 start, int64 end);
		public int64 Count() const { return spans.Count(); }

		private Core::Array<int64> spans;
		private int64 covered;
	}
}

// src/spanset.cf
include "spanset.ch"

namespace Spans
{
	public void SpanSet::Add(int64 start, int64 end) {
		spans.Push(start);
		spans.Push(end);
		covered += end - start;
	}
}
```

```c4
// src/tests/wordcount_tests.cf - [test] functions; `nuke test` (or build_run task "test") runs them
include "wordcount.ch"

[test("words.count.folds_repeats")]
static void CountsRepeats() {
	Core::HashMap<Core::String, int64> counts = Words::Count(Core::StringView("b a b"));
	assert_eq((int64)2, counts.Get(Core::String("b")), "b twice");   // (expected, actual, message), compared as int64
	assert_true(counts.Contains(Core::String("a")), "a once");
	assert_se("b", Words::Top(counts, 1)[0].word.CStr(), "b first");  // C strings
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
| `printf("%s", s)` with a String; `Format(&b, "{0:s}", s)` | pass `s.CStr()`: printf and Core::Format read C strings and print garbage for a String |
| `v.push_back(x)`, `v.size()`, `v.empty()`, `v.back()` | `v.Push(x)`, `v.Count()`, `v.IsEmpty()`, `v.Back()` |
| `m.find(k) != m.end()`, `m.count(k)`, `m.insert({k, v})` | `m.Find(k)` (a `V*`, null when absent), `m.Contains(k)`, `m.Insert(k, v)`; `m[k]` creates a zero value |
| `std::sort(v.begin(), v.end(), cmp)` | `v.Sort()` (by `<`) or `v.SortBy([](const T& a, const T& b) -> bool { return ...; })` |
| `[](int x) { return x * 2; }` returning a value | `[](int32 x) -> int32 { return x * 2; }` - a lambda that returns names its type |
| `std::stoi`, `atoi`, `std::to_string` | `Core::ParseInt64(view, value)` (false when not a number), `builder.AppendInt(n)` |
| `std::cin`, reading stdin | there is no standard-input API: read files (`FS::File`) |
| `T& r = c ? a : b;` to pick one of two objects | `?:` on objects makes a copy: pick through pointers, `T* r = c ? &a : &b;` |
| `p->x` on an object, `o.x` through a pointer | `.` on objects and references, `->` through pointers only |
| a static helper or `const` with the same name in two `.cf` files | one program, one scope per namespace: give each name one home (a header) or distinct names |

## 3. Bedrock every day

`include "bedrock.ch"` brings in Bedrock (not math: `include "math.ch"`). Counts and indices are `int64`; element
access is unchecked; a zero value of any container or string is empty. Full reference: the toolchain's
`docs/library/bedrock.md`; every type's and function's signatures: `docs/library/bedrock-api.md` (docs_read).

| Type | Everyday members |
|---|---|
| `Core::StringView` (a view; pass by value) | `StringView("lit")`, `Data()`, `ByteLength()`, `IsEmpty()`, `ByteAt(i)`, `Substring(start, length)` (clamped, no copy), `IndexOf(char, from)`, `IndexOf(view)`, `Contains`, `StartsWith`, `EndsWith`, `Trimmed()`, `==`, `<` |
| `Core::String` (owns UTF-8) | `String("lit")`, `String(view)`, `View()`, `CStr()`, `ByteLength()`, `IsEmpty()`, `+` (with String, view or literal: a new String), `+=`, `Clear()`, `==` / `<` with strings and literals, `hash` (a HashMap key) |
| `Core::StringBuilder` | `Append(view or literal)`, `AppendByte(c)`, `AppendInt(int64)`, `AppendDouble(d, decimals)`, `AppendIntBase(n, base, minDigits)`, `View()`, `CStr()`, `ToString()`, `Clear()`, `ByteLength()` |
| `Core::Format(&builder, "{0} {1:D} {2:F.2} {3:s}", i32, i64, dbl, cstr)` | appends; `{n}` int32, `{n:D}` int64, `{n:u}` uint64, `{n:x}` hex, `{n:F.d}` double, `{n:f.d}` float, `{n:s}` C string; `.N` pads integers to N digits. Arguments are not checked: an int64 under `{n}` is cut to 32 bits |
| `Core::Array<T>` | `= { 1, 2 }`, `Array<T>(capacity)`, `Push(v)`, `Pop()`, `Count()`, `IsEmpty()`, `[i]`, `Front()`, `Back()`, `Insert(i, v)`, `RemoveAt(i)`, `RemoveRange(first, end)`, `Clear()`, `Resize(n)`, `Reserve(n)`, `Sort()`, `SortBy(before)`, `IndexOf(v)`, `Contains(v)`, `LowerBound(v)`, `LowerBoundBy(v, before)`; range-for |
| `Core::HashMap<K, V>` | `Insert(k, v)` (true when added), `m[k]` (`V&`, created when absent), `Find(k)` (`V*` or nullptr), `FindAs(view)` for String keys, `Get(k)` (a copy, zero when absent), `TryGet(k, out)`, `Contains(k)`, `Remove(k)`, `Count()`; range-for gives `Core::KeyValue<K, V>&` (`.key`, `.value`) in no particular order |
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
- File-scope names live in their namespace for the whole program: a `static` function or a `const` in one `.cf` is
  visible to (and collides with) the same name in another `.cf` of the same namespace.
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
- Static members: `public static int32 Count;`, used as `Type.Count` or `Type::Count`.
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
packs (`template<...Ts>`); a member of a template instance compiles only when used. Default arguments work.

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
