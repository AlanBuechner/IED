# Bedrock - the C4 default library

Bedrock is the library that ships with the compiler. It provides containers, UTF-8 text, formatting,
files, serialization, the reflection wrappers, the GC interface, threading, the operating system's services and the
math library. The language itself has no built-in
string type (string literals are `char*`); Bedrock supplies the UTF-8 string types.

Location: `Bedrock/` in the repo. Umbrella headers: `Bedrock/bedrock.ch` (everything but math) and
`Bedrock/math.ch` (the `Math` library, D-46).

---

## 1. How Bedrock gets into a program

- Bedrock is opt-in by include, except its prelude: `Prelude/prelude.ch` (the builtins and `_GUID`,
  [language/14-builtins.md](../language/14-builtins.md)) is part of every program and every unit sees it.
- C4.exe resolves a program's `include "..."` lines against the install's `Bedrock/` too, and registers each library file a program source includes with
  everything reachable from it (its includes, and each header's paired `.cf`). `include "bedrock.ch"` brings
  in all of Bedrock except math, `include "math.ch"` the math library, and a single header such as
  `include "Container/list.ch"` only what it needs. Files nothing includes (work in progress) never enter
  the build, and a program that includes none of Bedrock compiles none of it.
- Bedrock is registered after the user's inputs, so a name both declare is the program's. Bedrock's names
  are visible only to files that include them (normal include-closure visibility, see
  [language/01-program-model.md](../language/01-program-model.md)).
- The compiler build copies `Bedrock/` next to `C4.exe`; `MakeToolchain.py` copies it into a toolchain (a
  Development `nuke build` from the repository root refreshes the `dev` one,
  [tools/02-nuke.md](../tools/02-nuke.md#5-toolchains)). A stale copy next to the exe or in the toolchain is a
  common source of "my change did nothing".

```c4
include "bedrock.ch"

int32 main() {
	Core::Array<int32> numbers = { 30, 10, 20 };   // delete runs at scope exit
	numbers.Push(40);
	numbers.Sort();
	for (int32 n : numbers) printf("%d\n", n);
	return 0;
}
```

---

## 2. Conventions

- **One-level namespaces** (D-58): `Core` (containers, text, hashing, sorting), `FS` (files),
  `Serialization` (DOM, JSON, YAML, binary), `Reflection` (the runtime registry), `GC` (the collector),
  `Threading` (threads, atomics, locks, futures, jobs), `Platform` (virtual memory, the machine and the process,
  libraries, wall-clock time) and `Math` (vectors, matrices, quaternions, transforms).
  Anything that is not part of a larger system goes in `Core`. Namespaces are open, so user code may add
  to any of them as long as member names do not collide.
- Constructors are `[operator("create")]` methods (overloads chosen by argument count and type), the
  destructor is `[operator("delete")]`. Value locals are RAII; heap objects are finalized by the GC or by
  `delete`.
- Iteration uses `[operator("begin")]` / `[operator("end")]` plus iterator `notequal`, `dereference` and
  `increment`. Indexing uses `[operator("index")]`.
- Containers allocate element storage with `raw_alloc` and register it as a GC scan region, so a
  `Core::Array<T*>` keeps its elements alive. `Array` and `StringBuilder` keep their buffers in `buffer_resize`
  memory ([builtins 4](../language/14-builtins.md#4-memory-and-gc)): past 1 MiB a buffer grows in place, its pages
  committed after it instead of the whole buffer copied, so pushing to a large array or appending to a large text
  costs no copies until the buffer outgrows its reservation (8 times its size, at least 64 MiB). They construct,
  copy, relocate and destroy their elements with the lifecycle intrinsics
  ([14.1](../language/14-builtins.md#1-compiler-intrinsics)), so element types with a create, copy or delete work
  as in C++ (`Core::Array<Core::String>`).
- **Owning types are values.** `Array`, `List`, `Queue`, `Stack`, `HashMap`, `String` and `StringBuilder` have a
  copy operator: a copy owns its own buffer. `FS::File`, `Serialization::DomDoc`, `Threading::Thread` and
  `Platform::Library` own a handle and cannot be copied: pass them by reference or move them with `take(x)`. `Mutex`,
  `ConditionVariable` and `WaitGroup` are places threads wait at: they cannot be copied either.
- **Zero is empty.** Every type is valid when zero - an empty container or string, a closed file - and a
  zero container allocates nothing until its first insert, so a moved-from value is usable and an unused
  member costs no allocation.
- **Counts, sizes and indices are `int64`** everywhere; "not found" is `-1`.
- **Element access is unchecked** (`At`, `[]`, `Front`, `Back`, `Top`), as in C++. Removals with an index
  out of range do nothing; `Pop` on an empty container gives a zero element.
- **Queries are const** (D-31): sizes, lookups, views and the reflection handles' accessors can be called
  on a const object or through a `const T&`; element access has a const twin that hands out read-only
  elements.
- A member of a template instance is compiled only when it is used (D-16), so `Array<T>::Sort()` (which needs `<`) exists for every
  `T` and only an element type that is actually sorted must define `<`.

---

## 3. Containers (`Container/`)

Every container keeps its elements in storage from `Core::AllocateElements<T>(count)` and gives it back with
`Core::FreeElements<T>(p)` (`storage.ch`): aligned to `alignof(T)`, through `raw_alloc_aligned` when that is more
than the 16 bytes `raw_alloc` guarantees, so an element such as a `packed float[8]` (32) or `packed float[16]`
(64) can be read with aligned AVX instructions.

Every container that owns native memory says how much through `[operator("native_bytes")] int64 NativeBytes() const`
(what `native_bytes_of` and the GC census read, [language/14](../language/14-builtins.md#1-compiler-intrinsics)): its
storage by capacity, plus what its live elements hold, walked only when `holds_native_bytes` of the element type is
true. `Array`, `List`, `Queue`, `HashMap`, `HashSet`, `BitSet`, `String`, `StringBuilder`, `Serialization::DomDoc` and
`Threading::JobQueue` have one; `Stack`, `PriorityQueue` and `LruCache` add up the containers they are made of. A
`List`'s walk follows at most `Count()` nodes.

### `Core::Array<T>` - growable contiguous array (`array.ch`)

| Group | Members |
|---|---|
| Create / delete | `Init()` (allocates nothing), `Init(int64 capacity)`, `Init(__init_list<T>)` (enables `Core::Array<int32> a = {1, 2, 3}`), copy, `Delete()` |
| Size | `Count()`, `Capacity()`, `ByteSize()`, `IsEmpty()` |
| Access | `At(int64 i)` and `a[i]` -> `T&`, `Front()`, `Back()`, `Data()`; on a const array they give `const T&` / `const T*` |
| Modify | `Push(v)` (takes `v` by value: a temporary or `take(x)` moves in, so a value that cannot be copied fits; safe when `v` is an element of the same array), `Pop()` (moves the last element out), `Insert(i, v)` (as `Push`), `Append(const Array<T>&)`, `Append(const T* items, count)` (both safe with the array's own elements; they grow as `Push` does), `RemoveAt(i)`, `RemoveRange(first, end)`, `RemoveSwap(i)` (O(1)), `Clear()` (destroys the elements, keeps the buffer), `Reserve(n)`, `Resize(n)` (default-creates or destroys), `ResizeUninitialized(n)` (grows without creating the new elements: for bytes and numbers written before they are read, such as a decoder's output; shrinks as `Resize`), `ShrinkToFit()`, `Swap(Array<T>&)` |
| Search | `IndexOf(v)` (`==`, `-1` when absent), `Contains(v)` |
| Algorithms | `Sort()` (by `<`), `SortBy(before)`, `Reverse()`, `LowerBound(v)`, `UpperBound(v)`, `LowerBoundBy(v, before)`, `UpperBoundBy(v, before)` |
| Iteration | range-for through `ArrayIterator<T>` (yields `T&`); a const array iterates through `ConstArrayIterator<T>` (yields `const T&`) |

`Core::ArrayLayout` and `Core::ResizeEmptyArray(array, count, elementBytes, elementAlign)` give code that knows
the element type only at run time (the reflection codec) an empty array's storage, zeroed.

Growth doubles the capacity (at least 4 on the first insert). Every container's insertions (`Queue::Push`,
`Stack::Push`, `List::PushBack` and the rest, `HashMap::Insert`) take their value the same way, so
`Core::Array<Net::TcpStream>` or `Core::HashMap<int64, Net::TcpStream>` filled with `take(stream)` work. `before(a, b)` is any callable that says
whether `a` comes first: a lambda, a `funcptr` or an object with `[operator("call")]`.

### `Core::List<T>` - doubly linked list (`list.ch`)

| Group | Members |
|---|---|
| Create / delete | `Init()` (allocates nothing), `Init(InitList<T>)`, copy, `Delete()` |
| Size | `Count()`, `IsEmpty()` |
| Access | `Front()`, `Back()` (references), `First()`, `Last()` -> `ListNode<T>*` (null when empty; `value`, `next`, `previous`) |
| Modify | `PushFront(v)`, `PushBack(v)`, `InsertBefore(node, v)` (null: the back), `InsertAfter(node, v)` (null: the front) - each returns the new node; `PopFront()`, `PopBack()` (move the element out), `Remove(node)`, `Clear()` |
| Relink | `MoveToFront(node)`, `MoveToBack(node)`, `Reverse()` - no element is copied or moved |
| Search | `Find(v)` -> the first node whose value `==` v, or null; `Contains(v)` |
| Iteration | range-for front to back, `T&` (or `const T&` on a const list) |

Inserting or removing never moves the other elements, so a `ListNode<T>*` stays valid until its own
element is removed: the handle an LRU cache keeps. Each node is one allocation registered with the GC.

### `Core::HashMap<K, V, Traits = KeyTraits<K>>` - open addressing (`hashmap.ch`, `hashtable.ch`, `hash.ch`)

| Group | Members |
|---|---|
| Create / delete | `Init()` (allocates nothing), `Init(int64 capacity)`, copy, `Delete()` |
| Size | `Count()`, `Capacity()`, `IsEmpty()` |
| Use | `Insert(k, v)` -> `true` when added, `false` when the value was replaced; `At(k)` and `m[k]` -> `V&` (default-creates an absent key); `Find(k)` -> `V*` or null (valid until the map changes); `TryGet(k, out)`; `Get(k)` (a copy, zero `V` when absent); `Contains(k)`; `Remove(k)`; `Clear()`; `Reserve(n)` |
| By another key type | `FindAs(q)`, `ContainsAs(q)`, `RemoveAs(q)` take a key of a type `Q` that hashes as `K` does (`KeyTraits<Q>`) and compares with `==` to a `K`: a `Core::StringView` for `Core::String` keys, so no `String` is made to look one up |
| Iteration | range-for yields `Core::KeyValue<K, V>&` (`key`, `value`) in table order; do not change a key through it. A const map yields `const KeyValue<K, V>&` |

The table (`hashtable.ch`, shared with `HashSet`) is in the style of Swiss tables: beside the entries, one control byte
per slot holds empty, removed, or 7 bits of the entry's hash. A lookup compares 16 control bytes at once (one SSE2
compare) and opens only the entries whose 7 bits match, so a miss rarely touches an entry; probing goes group by
group in growing steps over a power-of-two table. Live plus removed slots stay at most 7/8 of it, so every probe
ends at an empty slot, and growing moves the live entries to a table twice their number, leaving the removed slots
behind. Against `std::unordered_map` a million `int64` keys insert in a quarter of the time and missing ones are
found in an eighth ([test suites 8](../tools/05-test-suites.md#8-benchmarks)). `Traits` is a type with
`static uint64 Hash(const K&)` and `static bool Equal(const K&, const K&)`:

- `Core::KeyTraits<K>` (the default) uses `K`'s own `[operator("hash")]` and `==`. It is specialized for
  every integer type, `char8`, `bool`, `float` and `double` (`-0` hashes as `+0`; NaN never equals
  itself, as with `==`) and pointers. `Core::String` and `Core::StringView` define `hash`.
- `Core::ByteKeyTraits<K>` hashes and compares a key's bytes: for plain-data structs without padding.

`hash.ch` also has `Core::HashBytes(bytes, length)` (a 64-bit hash in the manner of wyhash: 16 bytes a step, 48 for
long input, each pair of words folded through a 128-bit product, `Core::FoldedMultiply(a, b)`), `Core::HashInteger(uint64)`
(the splitmix64 finalizer) and `Core::NextPowerOfTwo(int64)`. `sha1.ch` has the SHA-1 digest (FIPS 180-4) for
protocols that require it (the WebSocket handshake; not for anything that must resist a forger):
`Core::Sha1(bytes, count)` or a `Core::Sha1Builder` fed in pieces (`Add`, then `Finish` once), giving a
`Sha1Digest` (`bytes[20]`, `ToHex()`, `==`, `!=`).

### `Core::HashSet<T, Traits = KeyTraits<T>>` (`hashset.ch`)

An unordered set on the same table as `HashMap`, with the same `Traits`.

| Group | Members |
|---|---|
| Create / delete | `Init()` (allocates nothing), `Init(int64 capacity)`, `Init(InitList<T>)` (`Core::HashSet<int32> s = { 1, 2, 3 }`), copy, `Delete()` |
| Size | `Count()`, `Capacity()`, `IsEmpty()` |
| Use | `Add(v)` -> `true` when added, `false` when an equal value was there (the set keeps that one; takes `v` as `Push` does); `Contains(v)`; `Find(v)` -> `const T*` or null; `Remove(v)`; `Clear()`; `Reserve(n)`; `ContainsAs(q)`, `RemoveAs(q)` by another key type, as `HashMap`'s |
| Set algebra | `UnionWith(other)`, `IntersectWith(other)`, `ExceptWith(other)` change the set in place; `IsSubsetOf(other)` |
| Iteration | range-for yields `const T&` in table order: a value is found by its hash, so it is not changed in place |

### `Core::BitSet` (`bitset.ch`)

A growable array of bits worked on 64 at a time (`std::vector<bool>`, `std::bitset`). A zero set holds no bits;
a copy owns its words; bit indices are unchecked, as element access is.

| Group | Members |
|---|---|
| Create / size | `Init()`, `Init(int64 bits)` (all clear), copy, `Delete()`, `Count()` (bits held), `IsEmpty()`, `Resize(bits)` (new bits clear) |
| Bits | `Test(i)`, `Set(i)`, `Clear(i)`, `Flip(i)`, `Assign(i, value)`, `SetAll()`, `ClearAll()` |
| Queries | `CountSet()` (a population count a word), `Any()`, `None()`, `All()`, `NextSet(from)`, `NextClear(from)` (`-1` when there is none) |
| Whole sets | `&=` / `And`, `|=` / `Or`, `^=` / `Xor`, `AndNot(other)` (a shorter set counts as clear past its end), `==`, `!=` |

### `Core::Queue<T>` and `Core::Stack<T>` (`queue.ch`, `stack.ch`)

- `Queue<T>`: first in, first out, over a power-of-two ring buffer. `Init()`, `Init(int64 capacity)`,
  copy, `Delete`, `Count`, `Capacity`, `IsEmpty`, `Push`, `Pop` (moves the front out), `Front`, `Back`
  (references), `Clear`, `Reserve`. Range-for visits front to back (`const T&` over a const queue).
- `Stack<T>`: last in, first out, over an `Array<T>`. `Push`, `Pop`, `Top`, `Count`, `Capacity`,
  `IsEmpty`, `Clear`. Range-for visits bottom to top.

### `Core::InitList<T>` (`initlist.ch`)

The public name of the built-in braced-list view `__init_list<T>` (D-53): `data`, `size`, indexing and
range-for. A parameter of this type takes a braced list, `Sum({ 1, 2, 3 })`; the containers' list
creates take one.

### Random numbers (`Random/random.ch`)

`Core::Random` is xoshiro256** (256 bits of state, a period of 2^256 - 1, passes the statistical test suites)
seeded through splitmix64: fast numbers for games, simulations and tests, not for secrets (those come from
`Platform::RandomBytes` or `Crypto`). A generator is a value, never a global: seed it and pass it to what needs
numbers, so a run replays from its seed. `Core::RandomSeed()` reads a seed from the operating system.

| Members | |
|---|---|
| `Init(uint64 seed)` | the same numbers from the same seed on every machine |
| `Next()`, `Next32()` | 64 and 32 random bits |
| `Range(low, high)` | uniform in `[low, high)` with no modulo bias (Lemire's method); `low` for an empty range |
| `Unit()`, `UnitFloat()`, `RangeDouble(low, high)` | uniform in `[0, 1)` (every `k / 2^53`, or `k / 2^24` as a float) and in `[low, high)` |
| `Chance(p)`, `Normal(mean, deviation)` | true with probability `p`; a normal value (Marsaglia's polar method) |
| `Shuffle(items, count)`, `Shuffle(array)` | Fisher-Yates: every order equally likely; elements relocate, never copied |
| `Split()` | this generator jumps 2^128 steps ahead and the returned one starts where it was: one independent generator per thread or job |

### Sorting (`sort.ch`, `radixsort.ch`)

`Core::Sort(T* items, int64 count, F before)` is pattern-defeating quicksort (pdqsort): median-of-three (of nine
for long ranges) pivots, an insertion sort that finishes nearly sorted ranges, a partition that gathers the
elements equal to a repeated pivot, a few swaps that break a pattern when a partition comes out unbalanced, and
heapsort if that keeps happening. O(n log n) worst case, linear on sorted and all-equal input, not stable,
elements relocated and never copied. Elements of 16 bytes or less partition in blocks (BlockQuicksort): each side
records the offsets of its misplaced elements without branching on the comparisons, so a cheap comparator costs
no mispredicted branches; 2M 8-byte records sort by key in 0.4 of `std::sort`'s time. `Core::LowerBound` / `Core::UpperBound(items, count, value, before)` are the binary
searches. `Core::Ascending<T>` orders by `<`.

`Array::Sort()` and `Core::SortAscending(items, count)` go through `Core::AscendingSorter<T>`: pdqsort for any
`T`, and for the integers, characters, `float` and `double` an LSD radix sort (`Core::RadixSort`): a byte a pass
over keys that order as the numbers do (`Core::RadixKey<T>`: a signed integer's sign bit flipped, a negative
float's bits all flipped), one counting pass for every byte, and the passes whose byte all keys share skipped.
Linear in the count, stable, with a scratch buffer of the same size; below 512 elements pdqsort does it. Two
million `int32` sort in about a tenth of `std::sort`'s time ([test suites 8](../tools/05-test-suites.md#8-benchmarks)).

### `Core::PriorityQueue<T, Before = Ascending<T>>` (`priorityqueue.ch`)

A binary heap over an `Array` (`std::priority_queue`): `Push(v)` (as `Array::Push`), `Pop()` (moves out the element
no other comes before: the smallest with the default order; a zero `T` when empty), `Top()`, `Count`, `Capacity`,
`IsEmpty`, `Clear`, `Reserve`. `Before` is any type with `[operator("call")] bool(const T&, const T&)`;
`Init(Before order)` takes one with state. Range-for visits the elements in heap order.

### `Core::LruCache<K, V>` (`lrucache.ch`)

A map of at most `Capacity()` entries that drops the least recently used one to make room: a `HashMap` of nodes in
a `List` kept from the most to the least recently used, so every operation is O(1) and entries never move.
`Init(capacity)` (a capacity below one holds one), copy, `Find(k)` -> `V*` (marks it used), `Peek(k)` (does not),
`Contains`, `Put(k, v)` -> `true` when an entry was dropped, `Remove`, `Clear`, `Resize(capacity)`, `Count`,
`IsEmpty`. Range-for visits the entries from the most to the least recently used.

### Checksums (`crc32.ch`)

`Core::Crc32(data, count)` and `Core::Crc32Builder` (`Add` pieces, then `Value()`; any split gives the same value):
CRC-32 as zlib, gzip, PNG and ZIP compute it (IEEE 802.3, reflected polynomial `0xEDB88320`): folded with the
carry-less multiply (`simd_clmul`), 256 bytes a step where the processor has VPCLMULQDQ and AVX-512 and 64 where it
has PCLMULQDQ, the rest eight bytes a step by slicing-by-8 tables (on a Ryzen 7800X3D: 2.6 GB/s by the tables alone, 17 GB/s folding 128 bits
at a time, about 55 GB/s folding 512; zlib-ng about 65). For accidental changes only; `Crypto/` resists forgers.

---

## 4. Text (`Text/`)

### `Core::StringView` - non-owning UTF-8 view (`stringview.ch`)

A small value `{ bytes, byteLength, charCount }`; pass it by value. Making one costs nothing beyond `strlen` for a
`const char*`: `CharCount` counts the characters when asked, unless the view was made with its count (`String::View`
passes the one it keeps). A view of part of a string is not null-terminated, so read it through `Data()` and
`ByteLength()`.

`Init(const char*)`, `Init(bytes, byteLength)`, `Init(bytes, byteLength, charCount)`, `Data`,
`ByteLength`, `CharCount`, `IsEmpty`, `IsValidUtf8` (Unicode table 3-7: no overlong forms, surrogates or
code points above U+10FFFF), `ByteAt(i)`, `CodePointAt(charIndex)` (`-1` when out of range or
malformed), `IndexOf(char, start)`, `IndexOf(StringView)`, `Contains`, `StartsWith`, `EndsWith`, `==`, `!=`,
`<`, `>`, `<=`, `>=` (byte order; `==` and `!=` also against a `const char*`: `view == "ab"`), `hash`,
`Substring(start, length)` (clamped, no allocation), `Trimmed()`. Free functions:
`Core::Utf8CharCount`, `Core::IsValidUtf8`, `Core::InvalidUtf8Offset` (where the first ill-formed sequence
starts, or -1), `Core::DecodeUtf8(bytes, byteLength, at, width)` (the code point at byte `at` and its length;
U+FFFD of width 1 for a byte that starts no well-formed sequence, -1 at the end), `Core::IsSpace`.

### `Core::String` - owning UTF-8 string (`string.ch`)

A value: `Core::String s = Core::String("hello");` (no implicit conversion from `char*`, D-32). It owns a
null-terminated buffer and keeps its character count current; a zero `String` is the empty string.

`Init(const char*)`, `Init(StringView)`, `Init(int64 reserve)`, copy, `Delete`, `ByteLength`,
`CharCount`, `Capacity`, `IsEmpty`, `CStr()`, `View()`, `CodePointAt`, `==`, `!=`, `<`, `>`, `<=`, `>=` (`==` and `!=`
also against a `StringView` or a `const char*`: `s == "ab"`), `hash`, `+` with a `String`, a `StringView` or a
`const char*` (a new `String`), `Append` (`+=`) of a `String`, a `StringView` or a `const char*` (safe with itself
or a view of itself), `Clear`, `Reserve`.

### `Core::StringBuilder` (`stringbuilder.ch`)

A growable buffer for composing text. `Init(int64 capacity)`, copy, `Delete`, `Append(StringView)`,
`Append(const char*)`, `AppendByte`, `AppendBytes(bytes, count)` (embedded zero bytes kept), `AppendCodePoint`
(UTF-8; a surrogate or anything past U+10FFFF is U+FFFD), `Truncate(byteLength)`,
`AppendInt(int64)`, `AppendUInt(uint64)`, `AppendIntBase(v, base, minWidth)`, `AppendUIntBase`,
`AppendHex`, `AppendBin`, `AppendDouble(double, decimals)`, `AppendFloat(float, decimals)`, `Clear`,
`Reserve`, `ByteLength`, `IsEmpty`, `CStr()`, `View()` (both valid until the next change), `ToString()`
(a `String` copy).

A zero builder (`Core::StringBuilder b;`) is empty and allocates on its first append. Number appends
handle every value: binary up to 64 digits, `INT64_MIN`, `UINT64_MAX`. Floats are rounded to `decimals`
fraction digits by the C runtime (`1.995` at 2 is `2.00`, `2.5` at 0 is `2`); a negative `decimals`
writes the shortest text that reads back as the same double (`0.1`, `1000`, `0.3333333333333333`), in plain
decimals for exponents from -5 to 15 and scientific otherwise (`1e+16`, `1e-06`), as Python's `repr` does;
infinities and NaN are `inf`, `-inf` and `nan`.

### `Core::Format` (`format.ch`)

`void Core::Format(StringBuilder* out, const char* format, ...)` appends formatted text to a
caller-owned builder. A placeholder is `{index}` or `{index:kind.width}`; indices name the arguments in
call order, may come in any order and may repeat; `{{` and `}}` are literal braces. Up to
`Core::kFormatArgumentLimit` (16) arguments.

| Kind | Argument | Written as | `.N` |
|---|---|---|---|
| `d` (default) | `int32` | decimal | zero-pad to N digits |
| `D` | `int64` | decimal | zero-pad to N digits |
| `u` | `uint64` | decimal | zero-pad to N digits |
| `x` / `b` / `o` | `uint64` | hexadecimal / binary / octal | zero-pad to N digits |
| `f` / `F` | `float` / `double` | fixed point | N fraction digits (default 0) |
| `s` | `const char*` | the null-terminated text | - |

```c4
Core::StringBuilder b;
Core::Format(&b, "id={0:d.4} val={1:x.8} again={0}", 255, 4096);   // "id=0255 val=00001000 again=255"
```

A malformed placeholder is written as text.

### Regular expressions (`regex.ch`)

**Status:** **Implemented** (B55, D-71). `Core::Regex(pattern)` compiles once (`IsValid()`, else `Error()` and
`ErrorOffset()`, a byte offset into the pattern) and matches in time linear in the text: it runs as a Pike VM, so
no pattern can take exponential time, and there are no backreferences or lookaround. The syntax is RE2's: literals
and escapes (`\n \t \r \f \v \a`, `\xHH`, `\x{HHHH}`, escaped punctuation), `.` (no line break unless `(?s)`),
classes `[abc]` `[^a-z]` `[[:alpha:]]` and `\d \w \s \D \W \S` (ASCII), anchors `^ $` (the text's ends, lines under
`(?m)`), `\A \z \b \B`, groups `(x)` `(?:x)` `(?P<name>x)` `(?<name>x)`, alternation, `* + ? {n} {n,} {n,m}` (at
most 1000) and their lazy forms, and flags `(?i)` `(?m)` `(?s)` (for the rest of the group) or `(?i:x)`; `i` folds
ASCII letters. Text is UTF-8: `.` and classes match whole code points, and a byte that is not UTF-8 matches `.` and
nothing else.

| Member | Meaning |
|---|---|
| `Matches(text)`, `Match(text, match)` | the whole text |
| `Search(text, match, from = 0)`, `Contains(text)` | the leftmost match at or after byte `from`: alternatives and lazy or greedy repetition chosen as a backtracking engine would (leftmost-first) |
| `FindAll(text)` | every match, left to right, not overlapping; after an empty match the search moves a character on |
| `Replace(text, replacement)` | every match replaced; `$0`-`$9`, `${n}` and `${name}` are groups' texts, `$$` a `$` |
| `GroupCount()`, `GroupIndex(name)` | `(?:x)` takes no number |

A `RegexMatch` holds `Start(g)`, `End(g)` (byte offsets, -1 for a group that took no part), `Matched(g)` and
`Group(g)`, a view of the text searched - which must outlive it.

```c4
Core::Regex problem = Core::Regex("^(?P<file>[^(]+)\\((\\d+),(\\d+)\\): (error|warning) (\\w+): (.*)$");
Core::RegexMatch found;
if (problem.Search(line, found))
	Report(found.Group(problem.GroupIndex(Core::StringView("file"))), found.Group(2), found.Group(6));
```

### UUIDs (`uuid.ch`)

`Core::Uuid` (`bytes[16]` in text order) is a 128-bit identifier as RFC 9562 defines it. `Uuid::NewV4(random)` is
random; `Uuid::NewV7(unixMilliseconds, random)` starts with the time, so they sort by creation and keep database
indexes compact. Making one takes the `Core::Random` (and the time), so tests can replay them. `Uuid::Parse(text,
out)` reads the 36-character `8-4-4-4-12` form in either case, with or without braces; `AppendTo(builder)` and
`ToString()` write it lowercase; `Version()`, `IsNil()`, `==`, `!=`, `<` (byte order: version 7 by time) and
`hash`.

### UTF-16 (`utf16.ch`)

For the Win32 W functions and for protocols that count text in UTF-16 units (an LSP position's `character`):
`Core::ToUtf16(text, terminate = false)` gives an `Array<char16>` (null-terminated on request),
`Core::FromUtf16(units, count)` a `String`, `Core::Utf16Length(text)` the units, and `Core::Utf16OffsetOfByte(text,
byte)` / `Core::ByteOffsetOfUtf16(text, units)` convert offsets (an offset inside a character, or on the second unit
of a surrogate pair, maps to that character's start; past the end to the end). Ill-formed input - UTF-8 that is not,
an unpaired surrogate - becomes U+FFFD.

### Base64 (`base64.ch`)

`Core::Base64Encode(bytes, count)` gives RFC 4648 base64 (the standard alphabet, `=` padding) as a `String`;
`Core::Base64Decode(text, out)` appends the bytes to an `Array<uint8>` and returns false, leaving `out` as it was,
for text that is not padded base64.

### Integers (`integer.ch`)

`Core::ParseInt64(text, value)` reads decimal text - an optional `-` and at least one digit, nothing around them - and
returns false, leaving `value` as it was, for anything else or a number outside `int64`. The JSON readers use it.

---

## 5. Files, paths and directories (`FS/`)

Paths are UTF-8 on every platform (on Windows the runtime converts them for the wide API).

### `FS::File` (`file.ch`)

`FS::File` wraps the runtime `file_*` builtins (stdio). It closes itself on `delete`, so a value local is
RAII; it cannot be copied. Positions are 64-bit.

- `Init()` (closed), `Init(const char* path, const char* mode)` (closed when it cannot open; modes are C
  mode strings `r`, `w`, `a`, `rb`, `wb`, `ab`), `Delete()`, `Close()` (idempotent), `IsOpen`
- `Read(void* buffer, int64 count)`, `Write(const void* bytes, int64 count)`: bytes moved, `0` at the end
  of the file, `-1` on an error or a closed file; `Flush`; `Sync` (flush, then have the system write the data to the
  disk, so it outlives a power loss)
- `Seek(offset)` (from the start), `Seek(offset, FS::SeekOrigin::Start | Current | End)`: the new
  position or `-1`; `Tell()`
- `ReadAll(void* buffer, int64 capacity)`, `ReadText()` -> the rest of the file as a `Core::String`
- Free functions: `FS::FileExists(path)` (an existing regular file, not a directory), `FS::FileSize(path)`
  (`-1` when there is none), `FS::RemoveFile(path)`, `FS::WriteFileAtomically(path, bytes, count)` (the whole content
  written under a temporary name beside the file, synced, then renamed over it on the disk: a crash or a power loss
  leaves the old content or the new one, never part of either; false, with the old file as it was, when a step fails)

### `FS::Path` (`path.ch`)

A path as UTF-8 text (`std::filesystem::path`); everything works on the text alone. Both `/` and `\`
separate components when a path is read; `Join` writes `/`.

| Member | Meaning |
|---|---|
| `Init(const char*)`, `Init(StringView)`, copy | |
| `CStr()`, `View()`, `IsEmpty()` | |
| `IsAbsolute()` | starts at a root: `/a`, `C:/a`, `\\server\share\a` (`C:a` is relative to its drive) |
| `FileName()`, `Stem()`, `Extension()` | views: `icon.png`, `icon`, `.png`; `.bashrc` has no extension; `dir/` has no file name |
| `Parent()` | `a/b` of `a/b/c.txt`, `/` of `/c`, empty of `c.txt`; a root is its own parent |
| `/` (`Join`) | `Path("game") / "assets"` is `game/assets`; a part with a root replaces the path |
| `WithExtension(ext)` | replaced, added, or removed when `ext` is empty |
| `Normalized()` | `/` separators, no empty or `.` components, `name/..` folded (`.` when nothing is left) |
| `==`, `hash` | as text |

Free functions: `FS::CurrentDirectory()`, `FS::TempDirectory()`, `FS::RootLength(view)`, `FS::IsSeparator(byte)`.

### `FS::Directory` and the tree (`directory.ch`)

`FS::Directory(path)` is an open listing (`std::filesystem::directory_iterator`) of a directory's entries,
without `.` and `..`, in the order the file system keeps them; it closes on delete and cannot be copied.
`Next(DirectoryEntry&)` reads the next entry (`name`, `kind`: `EntryKind::File`, `Directory` or `Link`),
and range-for reads them all:

```c4
for (FS::DirectoryEntry& entry : FS::Directory("assets"))
	if (entry.kind == FS::EntryKind::File) printf("%s\n", entry.name.CStr());
```

| Function | Meaning |
|---|---|
| `IsDirectory(path)` | a directory (a link to one counts) |
| `IsLink(path)` | a symbolic link or junction; any other reparse point (a cloud placeholder, a deduplicated file) is the file or directory it stands for |
| `MakeDirectory(path)` | one directory; false when it exists or its parent does not |
| `MakeDirectories(path)` | with every missing parent; true when `path` is a directory at the end |
| `RemoveEmptyDirectory(path)` | |
| `RemoveAll(path)` | a file, a link (never what it points to) or a whole tree, read-only files too; the number of entries removed, 0 when there is nothing, -1 on a failure |
| `Rename(from, to)` | moves a file or directory; a file at `to` is replaced |

An entry name is at most `FS::kMaxEntryNameBytes` bytes of UTF-8.

### File information, copies and real paths (`info.ch`)

**Status:** **Implemented** (B53, D-69), over the runtime's `FileSystemRuntimeC.h`. Times are Unix time in
nanoseconds, the clock of `Platform::UnixTimeNs`. Paths the system makes come back as it writes them (`\` on
Windows).

| Function | Meaning |
|---|---|
| `GetInfo(path, info, followLinks = true)` | a `FileInfo` (`kind`, `readOnly`, `size`, `modifiedNs`, `createdNs`, `accessedNs`); false when nothing is there. A link is followed unless `followLinks` is false, then it is described itself (kind `Link`) |
| `SetModifiedTime(path, unixNs)` | a file's or directory's modification time, kept to 100 ns on Windows |
| `SetReadOnly(path, readOnly)` | the read-only attribute: `RemoveFile` and writes fail on a read-only file |
| `CopyFile(from, to, replace = true)` | the bytes, attributes and modification time; with `replace` false it fails when `to` exists |
| `CopyTree(from, to)` | a directory and everything in it into `to` (made when missing), replacing files there; links are neither followed nor copied. The files and directories copied, -1 when one could not be (the rest is copied), `from` is not a directory, or `to` is inside it (then nothing is made) |
| `AbsolutePath(path)` | against the working directory, `.` and `..` folded, without reading the file system |
| `CanonicalPath(path)` | the path the file system resolves to: absolute, links followed, each name in its stored case; empty when nothing is there |

### Watching a directory (`watch.ch`)

`FS::Watcher(path)` watches a directory and everything under it (`FS::Watcher(path, false)`: its own entries
only); it closes on delete and cannot be copied (move it with `take()`). `Next(change, timeoutMs)` waits for the
next `Change`: `kind` (`Added`, `Removed`, `Modified`, `Renamed`, `Overflow`), `path` relative to the watched
directory as the system writes it, and `oldPath` for a rename. It is false when none comes in time (`TimedOut()`)
or watching failed (`Error()`: the directory was removed, or the watcher is closed). Every change made after the
watcher opened comes out of `Next`, or an `Overflow` in its place when too many came at once to keep: then look at
the directory again. Changes come as the system reports them: saving a file is often several `Modified`, and an
editor's safe save a `Renamed` or a `Removed` and an `Added`, so coalesce them before acting. A watched directory
can still be removed; it goes once the watcher closes.

```c4
FS::Watcher assets = FS::Watcher("game/assets");
FS::Change change;
while (assets.Next(change, 500))
	if (change.kind == FS::ChangeKind::Modified) Reload(change.path);
```

Covered by `Bedrock/Tests/file_info_tests.cf` (7 tests: kinds, sizes and times, setting times and read-only,
junctions followed and not, copies of files and trees, absolute and canonical paths) and `watch_tests.cf` (6:
added, written, renamed and removed files, recursion, timeouts, a burst of 3000 files, the watched directory
removed, failures and moves), and `assets/IsolatedTest/Bedrock/Bug276ReparsePointNotLink` (a third-party reparse
point is a file, not a link).

### Reading a file in chunks (`chunks.ch`)

`FS::ChunkReader(path, chunkBytes = FS::kDefaultChunkBytes)` (1 MiB) reads a file front to back, each next chunk on
a thread of its own while the caller works on the one it has, so reading and decoding overlap (B62, roadmap 3).
`Next()` waits for the chunk read ahead and returns it, valid until the next call; it is empty at the end of the
file, after a failed read (`Failed()`) and once closed. The reader owns the file and the thread: it closes on delete
(`Close()` stops the thread, idempotent) and cannot be copied (move it with `take()`; the thread's state lives on
the heap). A file that cannot be opened gives a closed reader with no chunks.

```c4
FS::ChunkReader file = FS::ChunkReader("level.bin");
Serialization::BinaryStream records;
for (Core::StringView chunk = file.Next(); !chunk.IsEmpty(); chunk = file.Next()) records.Feed(chunk);
```

---

## 6. Serialization (`Serialization/`)

- **DOM** (`dom.ch`): an in-memory tree for unstructured data such as JSON or YAML.
  - `Serialization::DomDoc` is an arena of three buffers - 24-byte records, a string pool and one block of every
    container's children (a container that grows where it does not end the block moves to its end) - so a
    document of any size is three allocations: `NewNull`, `NewBool`, `NewInt(int64)`, `NewFloat(double)`,
    `NewString(const char*` or `StringView)` (copied into the pool), `NewObject`, `NewArray`,
    `AddMember(object, key, value)`, `Add(array, value)`, `NodeCount()`. A reader that collects a container's
    children while it reads them makes it whole: `NewObjectOf(members, count)` (`[Key(name), name length, value
    index]` per member) and `NewArrayOf(elements, count)`.
  - `Serialization::DomNode` is a `{doc, index}` handle: `IsValid`, `Kind`
    (`enum class DomKind { Null, Bool, Int, Float, String, Object, Array }`), `IsNull`, `AsInt`,
    `AsFloat`, `AsBool`, `AsString`, `AsView`, `Count`, `Member(key)`, `KeyAt(i)`, `ValueAt(i)`, `At(i)`.
    An invalid node - a missing member, an index out of range - reads as Null, and a lookup never adds a
    node.
  - Reflection bridge: `ReflectFrom(type, object)` builds a node tree from an object;
    `ReflectTo(type, node, destination)` fills an object from a tree and returns how many fields it set, or -1
    when a member does not fit its field. Both go through the reflection codec below and skip alias fields
    ([reflection](../runtime/02-reflection.md#2-registry-model)), so a union or a set of views is written once,
    through its first view, and a vector by its lanes (`{"x":1.0,"y":2.0,"z":3.0}`).
- **Reflection codec** (`reflectvalue.ch`, `reflecttext.ch`; B56, D-72): values of any reflected type to and from a
  DOM, by what their type names say: numbers and `bool`, `Core::String`, enums (an enumerator's name; a number
  reads too), `Core::Array<T>`, `T[N]`, `Core::HashMap<Core::String, V>` (a JSON object, in the map's order),
  objects and unions, nested to any depth. A pointer is written as null and never read; a type with no such form
  (a `func`, a map with other keys) the same. `Serialization::ToJson(value, indent)` / `FromJson(text, value)` and
  `ToYaml` / `FromYaml` do it in one call for any `T` (found through `nameof(T)`). Reading fills the members the
  text has and keeps the other fields; `null` keeps a field too. Arrays and maps it fills must start empty, and
  their new elements start zero rather than created, then take what the text has - a type whose zero is a valid
  value (every Bedrock value type is). A member that does not fit its field (a string for a number, a name that is
  no enumerator, more elements than a `T[N]`) makes the read false. The pieces: `DescribeValue(nameof(T))`,
  `WriteValue(doc, type, &value)`, `ReadValue(node, type, &value)`, `ReadFields`.

```c4
struct Settings { public Core::String name; public Core::Array<int32> sizes; public Core::HashMap<Core::String, Core::String> labels; }
Core::String json = Serialization::ToJson(settings, 2);
Settings loaded;
if (!Serialization::FromJson(json.View(), loaded)) printf("not settings\n");
```
- **JSON** (`json.ch`): `Serialization::WriteJson(node, out, indent)` (0 = compact). Floats are written
  in the shortest form that reads back as the same double, always with a fraction or exponent (`1.0`);
  NaN and the infinities, which JSON cannot express, are `null`. Control characters are `\uXXXX`.
  `WriteJsonString(text, out)` and `WriteFloatText(value, out)` are the pieces.
- **YAML** (`yaml.ch`): `Serialization::WriteYaml(node, out, indent)` writes block style with at least 2
  spaces per level; strings and keys are double-quoted, empty containers are `{}` / `[]`, and NaN and the
  infinities are `.nan`, `.inf` and `-.inf`.
- **Binary** (`binary.ch`): `Serialization::WriteBinary(type, object, out)` appends a reflected object's
  fields in declaration order (nested objects recursively, skipping alias fields; a named union's bytes
  whole) and returns the byte count;
  `Serialization::ReadBinary(StringView bytes, int64 position, type, destination)` reads them back and
  returns the position after them, or `-1` when the bytes end too soon. Plain data only (no pointers or
  owning types).
- **Streaming** (`jsonstream.ch`, `jsonfile.ch`, `binarystream.ch`; B62, D-77, roadmap 3): data decoded while it is
  still arriving, so a large file's first values are in use before the rest is read and memory holds about a chunk.
  - `Serialization::JsonStream` is a pull parser fed with chunks: `Feed(text)`, `Finish()` at the end, and `Next()`
    returns one `JsonEvent` (`BeginObject`, `EndObject`, `BeginArray`, `EndArray`, `Key`, `Null`, `Bool`, `Int`,
    `Float`, `String`, then `End`), `NeedMore` when the text so far ends inside one, or `Error`. A Key's or String's
    text is `Text()` (until the next call), numbers `IntValue()` / `FloatValue()`, `BoolValue()`, and `Depth()` the
    open containers. It accepts and refuses exactly what `ReadJson` does, with the same error, offset, line and
    column (`Failure()`, a `ReadResult`), whatever the chunk sizes; text that is not UTF-8 fails as soon as it is
    fed. `ReadValue(doc, node)` collects the next whole value into a `DomDoc` and `SkipValue()` passes over one, each
    across as many feeds as it takes; where no value stands they return the event that does (`Key`, `EndArray`,
    `EndObject`, `End`), which ends a loop over an array's elements. `Stop(why)` fails the stream for a source that
    broke. A string fed in small pieces is scanned once, and the read part of the text is dropped as it goes.
  - `Serialization::JsonFileStream(path, chunkBytes)` feeds a `JsonStream` from an `FS::ChunkReader`: the same calls,
    which wait for the chunk being read instead of returning `NeedMore`, and `ReadNext(value)`, the next whole value
    decoded into any reflected type (the rules of the reflection codec). A value that does not fit its type ends the
    stream with an Error. `ReadStreamed(json, doc, value)` does the same over any `JsonStream`.
  - `Serialization::BinaryStream` reads `WriteBinary`'s records as they arrive: `Feed`, `Finish`, and
    `Read(type or name, destination)` returns `BinaryRead::Record`, `NeedMore`, `End` (finished, no bytes left) or
    `Error` (the type is not reflected, or the bytes ended inside a record).
  - `DomDoc::Clear()` empties a document and keeps its memory, for a loop that reads one value at a time.
  - On a 48 MB file of 500,000 entities: the stream's 8.5 million events in 0.25 s (`ReadJson`, file read and DOM
    built: 0.5 s); the first entity decoded 2 ms after opening against 0.5 s when the file is read first; all of them
    decoded into reflected structs in 0.89 s (from the DOM: 0.92 s), holding one chunk instead of the file and
    5 million nodes.

```c4
Serialization::JsonFileStream level = Serialization::JsonFileStream("level.json");   // {"entities": [...]}
level.Next(); level.Next(); level.Next();                                           // {, "entities", [
Entity entity;
while (level.ReadNext(entity) == Serialization::JsonEvent::Value) { Spawn(entity); entity = Entity(); }
```
- **Readers** (`jsonreader.ch`, `yamlreader.ch`): `Serialization::ReadJson(text, doc)` and
  `Serialization::ReadYaml(text, doc)` build a tree in `doc` and return a `Serialization::ReadResult`:
  `root`, and on failure `error` (static text), `offset`, `line` and `column` (1-based, in bytes); `Ok()`.
  Both check that the text is UTF-8, skip a byte order mark, keep members in order with duplicates (`Member`
  finds the first), and refuse nesting deeper than `kMaxJsonDepth` / `kMaxYamlDepth` (512).
  - JSON is strict RFC 8259: no comments, trailing commas, single quotes, leading zeros, `NaN` or unescaped
    control characters; `\u` escapes (surrogate pairs included) become UTF-8. An integer that fits an
    int64 is an Int, every other number a Float.
  - YAML reads one document of YAML 1.2 with the core schema: block mappings and sequences (a sequence may
    sit at its key's indentation), flow `[...]` / `{...}` over several lines, plain scalars folded over
    lines, single- and double-quoted scalars with every escape, literal `|` and folded `>` block scalars
    with `+` / `-` chomping and an indentation digit, comments and `---` / `...`. Plain scalars resolve to
    null (`~`, `null`, empty), bool (`true`, `False`), int (`42`, `0x1F`, `0o17`), float (`1.5`, `.inf`,
    `.nan`) or string; a quoted scalar is a string. Anchors, aliases, tags, `?` keys, directives, a second
    document and tab indentation are errors that name what they are. Everything `WriteYaml` writes reads
    back as the same tree.

```c4
Serialization::DomDoc doc;
Serialization::DomNode root = doc.NewObject();
doc.AddMember(root, "name", doc.NewString("c4"));
doc.AddMember(root, "version", doc.NewInt(3));
Core::StringBuilder json;
Serialization::WriteJson(root, &json, 0);          // {"name":"c4","version":3}
int64 version = root.Member("version").AsInt();    // 3

Serialization::DomDoc config;
Serialization::ReadResult read = Serialization::ReadYaml(Core::StringView("window:\n  width: 1280\n"), config);
if (!read.Ok()) printf("%d:%d %s\n", read.line, read.column, read.error);
int64 width = read.root.Member("window").Member("width").AsInt();   // 1280
```

### JSONC and JSON Schema (`jsonc.ch`, `schema.ch`, `schemavalidate.ch`, B68)

`Serialization::ReadJsonc(text, doc)` reads JSON with `//` and `/* */` comments, which settings files and manifests
use. `StripJsonComments(text, out, error, at)` blanks the comments in place, keeping a block comment's line breaks, so
a reported offset or line still points into the original text. A comment or string that never ends is an error at its
start.

`Serialization::SchemaRegistry` loads JSON Schema documents (`Load(path)` from JSONC files, `Add(name, text)` from
memory) and resolves `$ref`s within and across them (`"#/$defs/x"`, `"../common.json#/$defs/y"`; names are folded:
`/` separators, no `.` or `..`, ASCII case). The dialect is the draft 2020-12 subset that IED's
`tools/pylib/minischema.py` knows:

- assertions: `type enum const pattern minLength maxLength minimum maximum exclusiveMinimum exclusiveMaximum
  required properties additionalProperties items minItems maxItems uniqueItems oneOf anyOf allOf not $ref`;
- annotations: `$schema $id $defs $comment title description default examples deprecated readOnly writeOnly format
  markdownDescription enumDescriptions`, and any key starting with `x-` (such as `x-ied-tiers`).

Loading refuses any other keyword, and a `pattern` that does not compile (patterns are `Core::Regex`, searched for
anywhere in the string), so a schema never relies on a check nothing makes.

`Serialization::ValidateJson(registry, name, pointer, value)` returns the `SchemaProblem`s (`path`, a JSON pointer
into the value with `~0` and `~1` escapes, and `message`); none means valid. The checks and messages match
minischema's: an integer-valued float is an `integer`, string lengths count characters, `oneOf` reports the branch
its `const` discriminator picks (else the one with the fewest problems), and a `$ref` that does not resolve is a
problem. `JsonEquals` (numbers by value, objects regardless of order) and `JsonTypeName` are public.
`tools/schema-check/crosscheck.py` in the IED tree runs both validators on the host API's examples and 450 mutations
of them, and they agree on every problem path.

---

## 7. Memory (`Memory/gc.ch`, `Memory/pool.ch`)

The `GC` namespace: the collector's C API ([runtime/01](../runtime/01-memory-and-gc.md#3-what-c4-code-can-reach))
as `[link]` declarations, with no wrappers.

| Function | Meaning |
|---|---|
| `Collect()` | collect now (also while disabled) |
| `GetStats()` | `GC::Stats { allocs, collections, heapBytes, freeBytes, pauseNs, liveBytes, lastPauseNs, nativeBytes }`; `nativeBytes` is the native memory in use beside the GC heap: `raw_alloc`'s blocks and container buffers (`Array`, `String`, `HashMap`, ...) |
| `ThreadAllocated()` | `GC::AllocCounts { objects, bytes, nativeBytes, nativeFreed }`: the calling thread's allocations since it started, never reset (a pool slot counts at its size when `Take` hands it out), the native memory it took (`raw_alloc`'s blocks and the bytes container buffers grew by) and the native memory it gave back (by the same measure, whichever thread took it); its own counters, no lock, so a profiler can read it around any scope |
| `Allocated()` | every thread's `AllocCounts` so far, threads that ended included (takes the collector's lock) |
| `SetCensus(bool on)`, `LastCensus(CensusEntry* out, int32 capacity)` | the live objects by type: while on, every `new` notes its type, and each collection counts the live objects by it into `GC::CensusEntry { type, objects, bytes, nativeBytes }` (`type` the qualified name, `T[]` for an array of T, `lambda in Ns::F` (`lambda #2 in Ns::F`, ...) for a lambda's closure, `nullptr` for objects of no known type: made before it was on, by native code or in a Pool; `nativeBytes` the native memory they hold, `native_bytes_of` each), one entry a name, by name; `LastCensus` returns how many entries there are (read it in a collection-result callback). It costs 8 bytes beside each slot of the spans allocated from and a walk of the live objects at each collection, with a call of `native_bytes_of` for each one whose type holds native memory |
| `SetAllocSampling(uint64 everyBytes)` | records about one allocation in every `everyBytes` a thread allocates, with its call stack, at points drawn at random so each sample stands for `everyBytes` bytes on average; 0 (default) turns it off; returns the previous setting. The calling thread takes a change up at once, another within 64 KiB |
| `TakeAllocSamples(AllocSample* out, int32 capacity)` | moves up to `capacity` of the oldest waiting samples to `out` as `GC::AllocSample { bytes, timeNs, threadId, frameCount, frames[32] }` (return addresses, the allocating function's first; `timeNs` on `LastPauses`' clock); returns how many. The latest 4096 are kept |
| `LastPauses(Pause* out, int32 capacity)` | the last collection's stops of the program, oldest first, as `GC::Pause { startNs, endNs }` in nanoseconds of the high-resolution counter (`Platform::Ticks()` scaled by 1e9 / `TickFrequency()`); returns how many there were: one for a stop-the-world collection, one per root copy and one to finish for a concurrent cycle. Read it in a collection callback for exact GC lanes in a profiler |
| `SetPercent(int32)` | live-heap growth that starts the next automatic collection (default 100); returns the previous value |
| `SetMarkThreads(int32)` | threads that mark a heap past 8 MiB, the collecting one included (-1 default: the processors, up to 8; 1: alone); returns the previous value |
| `Disable()`, `Enable()` | suppress / resume automatic collection |
| `SetConcurrent(int64 bytes)` | automatic collections of a live heap of at least `bytes` mark beside the program, with stops under about a millisecond (D-79): -1 never, 0 always, default 16 MiB; returns the previous value. `Collect()` always stops the world |
| `SetVerify(bool)` | check every concurrent mark against a fresh one; a missed object is a fatal error (tests; slow) |
| `Pin(void*)`, `Unpin(void*)` | keep an object and what it refers to alive (counted; any pointer into it) |
| `IsReachable(void*)` | whether a pointer is into a live object (for collection callbacks) |
| `RegisterRegion`, `UnregisterRegion` | raw memory scanned for pointers |
| `RegisterRootEnumerator(key, RootEnumerator, user)`, `UnregisterRootEnumerator(key)` | handle-based storage reports its live objects each collection through `visit(visitContext, object)` |
| `RegisterCollectionResult(key, CollectionCallback, user)`, `UnregisterCollectionResult(key)` | callbacks after each collection's sweep |

```c4
include "Memory/gc.ch"

void Enumerate(void* pool, GC::VisitRoot visit, void* ctx) { visit(ctx, ((Pool*)pool)->Live()); }

GC::RegisterRootEnumerator((void*)&g_pool, Enumerate, (void*)&g_pool);
GC::Stats s = GC::GetStats();
```

### Pools (`Memory/pool.ch`)

`GC::Pool(slotBytes, count, options)` (B63, D-78) is a block of `count` slots of `slotBytes` (rounded up to 16, at
most 1 MiB) for objects of one kind kept together - an engine's components, particles - that the collector still
tracks one by one. `Take()` gives a free slot, zeroed (nullptr when every slot holds an object something reaches:
it collects once before saying so), `Give(object)` frees one now; `At(index)`, `IndexOf(object)` (-1 for anything
but a slot's start), `Holds(object)`, `SlotBytes()`, `Count()`, `Close()`. A collection marks taken objects like
any other and ends those nothing reaches, freeing their slots. `GC::PoolOptions`:

| Option | Meaning |
|---|---|
| `noscan` | the objects hold no managed pointers: marking does not scan them |
| `keepAll` | a taken object, and what it reaches, stays until it is given back (the pool is a root) |
| `keep` | `func<bool(void*)>`: asked about each taken object a collection would end; true keeps it and what it reaches. It runs during the collection while every other thread is stopped: it must not lock or `Take` |
| `released` | `func<void(void*)>`: told of each object a collection ended, after the collection, one at a time; its slot is free again |

`delete` and finalizers do not apply to pool objects: their allocator ends them. Taken slots count as live bytes in
the collector's pacing. A pool owns its memory and gives it back on delete (its objects end with it), so it
cannot be copied (move it with `take()`).

```c4
GC::PoolOptions options;
options.released = [](void* p) { Recycle((Particle*)p); };
GC::Pool particles = GC::Pool(sizeof(Particle), 4096, options);
Particle* p = (Particle*)particles.Take();
```

---

## 8. Reflection (`Reflection/reflect.ch`)

The `Reflection` namespace (`Type`, `Field`, `Method`, `Param`, `EnumValue`, `Variable`, `Function`,
`Group`, `FindType`, ...). Documented in [runtime/02-reflection.md](../runtime/02-reflection.md).

---

## 9. Math (`Math/`, `include "math.ch"`)

Left-handed (+X right, +Y up, +Z forward), column-major like GLM, angles in radians, depth 0
to 1 (Direct3D, Vulkan). Every vector is a packed lane view, so vector arithmetic is SIMD, and every swizzle
works ([vectors](../language/11-vectors-and-swizzling.md)). The component-wise functions, comparisons, dot
products, the matrix inverse and the matrix products are written with the SIMD intrinsics
([builtins 11](../language/14-builtins.md#11-simd-intrinsics)); products fuse where the target has FMA. Matrix
and quaternion formulas follow GLM, and the benchmarks in `Benchmarks/math/` check that both compute the same
results.

```c4
include "math.ch"

Math::Mat4 world = Math::TransformMatrix(position, Math::FVector3(0.0f, Math::Radians(90.0f), 0.0f), Math::FVector3(1.0f));
Math::Mat4 view = Math::LookAt(eye, target, Math::FVector3::Up);
Math::Mat4 projection = Math::Perspective(Math::Radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
Math::FVector4 clip = projection * view * world * Math::FVector4(local, 1.0f);
Math::Quat turn = Math::AngleAxis(Math::kHalfPi, Math::FVector3::Up);
Math::FVector3 facing = turn * Math::FVector3::Forward;       // (1, 0, 0)
```

| File | Contents |
|---|---|
| `scalar.ch` | `kPi`, `kTau`, `kHalfPi`, `kE`, `kDegreesToRadians`, `kRadiansToDegrees`, `kEpsilon` (1e-6), `kFloatEpsilon`; templates `Min`, `Max`, `Clamp`, `Saturate`, `Abs`, `Sign`, `Lerp`, `InverseLerp`, `SmoothStep`, `Radians`, `Degrees`, `NearlyEqual`; float and double `Sqrt`, `Sin`, `Cos`, `Tan`, `Asin`, `Acos`, `Atan`, `Atan2`, `Pow`, `Exp`, `Log`, `Floor`, `Ceil`, `Round`, `Trunc`, `Fract`, `Mod` (GLSL `mod`: the sign of the divisor) |
| `vector.ch` | `TVector2<T>`, `TVector3<T>`, `TVector4<T>` (lanes `x y z w`, `r g b a`, the whole vector as `data`); aliases `FVector2..4` (float), `DVector` (double), `IVector` (int32), `UVector` (uint32), and `Vector2..4` for `FVector`; `+ - * /` component-wise and by a scalar, negate, compound assignment, `==`, `!=`, `[i]`; statics `Zero`, `One`, `UnitX..W`, `Right`, `Left`, `Up`, `Down`, `Forward`, `Backward`; `Dot`, `Cross` (3D, and the 2D z), `Length`, `LengthSquared`, `Distance`, `DistanceSquared`, `Normalized` (zero stays zero), `Angle`, `Reflect`, `Project`, component-wise `Min`, `Max`, `Clamp`, `Abs`, `Lerp`, `Floor`, `Ceil`, `Round`, `Fract`, `Radians`, `Degrees`, `NearlyEqual` |
| `matrix.ch` | `Mat2`, `Mat3`, `Mat4` (`columns[c]`, `m[c]`, `At(row, column)`, `Set`, `Mat4::Row`); `Mat(diagonal)`, `Mat4(Mat3)`, `ToMat3`; `*` by a matrix, a vector or a scalar, `+`, `-`, `*=`, `==`; `TransformPoint`, `TransformDirection`, `Determinant`, `Transposed`, `Inverse` (a singular matrix gives non-finite values, as in GLM); statics `Identity`, `Zero`; `NearlyEqual` |
| `quaternion.ch` | `Quat` (x, y, z, w; the default is the identity); `a * b` (b, then a), `q * v`, `+`, `-`, negate, scale; `Conjugate`, `Inverse` (zero gives the identity), `Normalized`, `Vector`; `Dot`, `Length`, `AngleAxis`, `Angle`, `Axis`, `FromEuler`, `ToEuler` (of a quat or a rotation matrix), `Pitch`, `Yaw`, `Roll`, `Lerp`, `Slerp` (shorter arc), `ToMat3`, `ToMat4`, `FromMat3`, `FromMat4`, `NearlyEqual` |
| `transform.ch` | `Translation`, `Scaling`, `Rotation(angle, axis)`, `RotationYXZ`, `TransformMatrix` (translation * rotation * scale, from Euler angles or a quat), `LookAt`, `Perspective`, `Orthographic`, `struct Transform` and `Decompose`, `TranslationOf` |
| `batch.ch` | `TVector3Batch<T, N>`, `TVector4Batch<T, N>`: N vectors in structure-of-arrays form (`x`, `y`, `z`, `w` are `packed T[N]`; lane i is vector i); aliases `FVector3x4`, `FVector3x8`, `FVector3x16`, `FVector4x4`, `FVector4x8`, `FVector4x16`; `+ - *` (by a batch, a scalar, or per vector by lanes), `/`, negate, `+=`, `-=`, `*=`, `Get(i)`, `Set(i, v)`, `Load(const TVector3<T>*)` and `Store` (N vectors from and to an array of vectors), `Xyz`; per vector `Dot`, `Cross`, `Length`, `LengthSquared`, `Distance`, `Normalized` (zero stays zero), `Lerp`, `Min`, `Max`, `Select(mask, a, b)`; through a `Mat4` `TransformVector`, `TransformPoint`, `TransformDirection`, and `Rotate(Quat, batch)`; `StridedLanes<N>(stride)` |

```c4
// A particle step on eight particles at a time; the batches stay in an array between frames.
Math::FVector3x8& p = positions[b];
Math::FVector3x8& v = velocities[b];
v.y = simd_muladd(simd_splat(-9.8f, 8), dt, v.y);
p += v * dt;
p.y = simd_select(p.y < 0.0f, -p.y, p.y);
packed float[8] distances = Math::Length(Math::TransformPoint(view, p));
```

- **Euler angles** are a `FVector3` of x pitch (about X), y yaw (about Y) and z roll (about Z), applied roll
  first, then pitch, then yaw: `RotationYXZ`, `FromEuler`, `ToEuler`, `TransformMatrix` and `Decompose` all
  agree, so `ToEuler(FromEuler(e)) == e` for pitch inside +-90 degrees. At gimbal lock (pitch +-90 degrees)
  `ToEuler` gives roll 0 and puts the whole turn in yaw. GLM's `quat(vec3)` builds roll * yaw * pitch
  instead; pairing that constructor with a YXZ extraction gives a quaternion Euler round trip that only
  holds for single-axis turns.
- `FVector3` is 12 bytes and `Mat3` 36, so arrays of them match tightly packed C and shader layouts;
  `FVector4`, `Quat` and `Mat4` are 16, 16 and 64.
- The component-wise functions answer as the scalar ones do lane by lane: `Min(a, b)` is `b < a ? b : a` and
  `Max(a, b)` is `a < b ? b : a` (std::min / std::max, so a NaN or a -0 in `a` stays); `Abs` clears the sign
  (`Abs(-0)` is `+0`); `Round` takes halves away from zero. `Dot` adds the products in `simd_reduce_add`'s halving
  order, (x + z) + (y + w).
- **Performance** (`Benchmarks/math/`, baselines in [test suites](../tools/05-test-suites.md#8-benchmarks)):
  against GLM's SSE paths, transforms, products, point transforms and quaternion rotation are as fast or
  faster and normalized crosses, slerp and the matrix inverse within 5 percent. Eight vectors at a time, the
  batches run within 5 percent of hand-written AVX2 intrinsics and 1.4 to 2.6 times as fast as GLM's per-vector
  loops.

---

## 10. Threading (`Threading/`)

**Status:** **Implemented** (B29, D-61). Built on the atomic intrinsics ([14 section 10](../language/14-builtins.md))
and the runtime's threads and address waits ([runtime/01](../runtime/01-memory-and-gc.md#3-what-c4-code-can-reach)).
Nothing in it takes an OS lock: a blocked thread parks on an address and is woken through it.

| Type | Meaning |
|---|---|
| `Atomic<T>` | one value of an integer, `bool`, enum, pointer or float type: `Load`, `Store`, `Exchange`, `CompareExchange(expected&, desired)`, `CompareExchangeWeak`, and for integers `FetchAdd`, `FetchSub`, `FetchAnd`, `FetchOr`, `FetchXor`; each takes a `MemoryOrder` (default `SeqCst`). A copy is an atomic snapshot. |
| `Thread` | `Thread::Run(func<void()> body)` starts a thread (attached: the collector scans it, its thread_locals start); `Join()`, `Detach()`, `IsJoinable()`; destroying a joinable thread joins it; not copyable (move it with `take`). Static: `CurrentId()`, `HardwareCount()`, `Yield()`, `Sleep(int64 ns)`, `SetCurrentName(const char*)` (names the calling thread for debuggers, profilers and crash dumps; false when the system cannot) and `CurrentName()` (`""` when it has none). |
| `Mutex` | one word: free, locked, or locked with sleepers. `Lock` spins briefly, then parks; `TryLock`; `Unlock` wakes one sleeper only when there is one. Not copyable. |
| `LockGuard` | locks a `Mutex` for its scope. |
| `UnlockGuard` | lets go of a `Mutex` the thread holds for its scope and takes it back at the end, an exception's included: slow work inside a locked region that touches nothing the mutex guards. |
| `ConditionVariable` | `Wait(Mutex&)`, `WaitFor(Mutex&, int64 ns)`, `NotifyOne()`, `NotifyAll()`; a sequence word sleepers park on. Waits may wake early: check the condition in a loop. |
| `WaitGroup` | a count of unfinished work: `Add(n)`, `Done()`, `Wait()`, `IsDone()`. |
| `Promise<T>`, `Future<T>` | one value handed from one thread to others: `Promise<T>::Set(value)` once; `Future<T>::IsReady()`, `Wait()`, `WaitFor(ns)`, `Get()`. Copies share the state. |
| `JobSystem` | worker threads that run `func<void()>` jobs (below). |

**The job system** is tuned so a time-critical job never waits for a worker behind long work:

- `JobSystem(JobSystemDesc desc)` starts `desc.frameWorkers` frame workers (default: the processors minus one,
  at least 1) and `desc.backgroundWorkers` background workers (default 1; with 0, frame workers run background
  jobs after their frame work); destroying it finishes the queued jobs and joins them. A zero (moved-from)
  system runs a submitted job at once on the submitting thread. Each worker names its thread ("Job Worker 0",
  "Background Worker 0"), which debuggers, crash dumps and the profiler show.
- `SetJobObserver(JobObserver)` tells a profiler of every job in the process: `begin(name)` on the worker before a
  job runs, `end(token)` with what `begin` returned once it has; a null function is not called, and an empty observer
  stops it. `Submit(job, priority, group, name)` gives the job the name the observer sees (a literal, or text that
  outlives the job). A profiler installs one as it starts, so each job is a task in its timeline.
- An exception that escapes a job ends the process, as one escaping a thread's entry does.

**Threads and exceptions; threads C4 did not start.** An exception that leaves a `Thread::Run` body, a job or an
event loop's `Run` called on its own thread ends the process (`C4: fatal: an exception was thrown and not caught`),
as in C++. Long-running servers wrap each entry: `Thread::Run([]() { try { Serve(); } catch (...) { Report(); } })`,
and keep a callback's own failures inside it. A thread that native code started (a Win32 callback thread, a
library's worker) must call `thread_attach()` before it touches C4 objects - the collector then scans it and its
thread_locals start - and `thread_detach` it before it ends ([runtime/01](../runtime/01-memory-and-gc.md#3-what-c4-code-can-reach));
a thread C4 started is attached already.
- `Submit(func<void()> job, JobPriority priority, WaitGroup* group)`: `High` and `Normal` are frame work, short
  jobs that must finish this frame; `Background` is work that may take long (loading, decompression,
  pathfinding). A frame worker runs only frame work, `High` before `Normal`; a background worker runs
  background work, and frame work when it has none, so frame work has every worker but a long job holds only
  background workers.
- `Wait(WaitGroup&)` helps: while the group is unfinished the waiting thread runs frame jobs itself (never a
  background one, whose length is unbounded), then spins briefly and parks. A frame job that waits on other
  frame jobs therefore never idles a worker.
- Each frame worker owns a bounded lock-free queue per frame priority (D. Vyukov's multi-producer multi-consumer
  queue: a cell's sequence number says whose turn it is, so a job is stored by value): a job it submits goes on
  its own queue, and idle workers take from the others'. Jobs submitted by other threads go through one shared
  queue per priority. When a queue is full, a frame job runs at once on the submitting thread and a background
  submit yields until there is room. The cells are scanned by the collector and cleared once taken.
- An idle worker spins for a short while (a frame's next job usually arrives within microseconds), then parks
  on the system's work word; a submit wakes one parked worker only when one is parked.

---

## 11. Scripting (`Scripting/`, `include "scripting.ch"`)

**Status:** **Implemented** (B30). The C4 face of the script host ([runtime/04-scripting.md](../runtime/04-scripting.md)),
over `C4Script.dll`'s C API. `bedrock.ch` does not include it: a program that includes it must be built with
`--script-host` (otherwise the build stops: `this program uses the scripting API (Scripting::Host), which needs a
script host: build it with --script-host`).

| Type | Meaning |
|---|---|
| `Host` | `Host::Open()` reads the interface beside the program (`Game.exe` -> `Game.c4i`), `OpenAt(path)` another one; `IsOpen()`, `Error()` (why it did not open: no interface, another build's, or no `C4Script.dll` beside the program). `Compile(options)` compiles, links and loads a script; `Reload(script)` compiles the script's options again and, when the new one compiles, unloads the old one and loads the new one (when it does not, the old one stays loaded and the new one carries the diagnostics). `UseCache(folder)` keeps compiled scripts in `folder`: a script compiled again with the same files, options, host and compiler links the machine code kept from before (docs/runtime/04 section 4.6). `DebugScripts()` / `SetDebugScripts(bool)`: whether each script is linked into a DLL with a PDB, which debuggers read as any module's, instead of in memory (on at first when the program was built with `-g`; docs/runtime/04 section 6.4). Closed on delete. |
| `ScriptOptions` | `name` (the script's name and reflection tag), `optimize` (`-O2`, default; false for `-O0`), `AddFile(path)`, `AddText(fileName, text)` (a file given whole; other files include it by that name), `Allow(projectOrPath)` (the host headers it may include; none: all), `Define("NAME=VALUE")`. |
| `Script` | `Ok()`, `Diagnostics()` (the compiler's messages, warnings too), `Tag()`, `Cached()` (linked from the host's cache), `Faulted()`, `FaultText()`; `Find(symbol)`, `Address(name)` (a function's address for a `funcptr`), `FindFunction(name)`, `FindType(name)` (the script's own, by reflection); `Call(fn, context)` runs `fn(context)` guarded, `Invoke(function, result, args...)` calls a reflected function guarded; `RunTests(filter)` runs the script's `[test]` functions with the harness (the failed assertions; -1 when it has none or a test faulted); `Unload()` (how many finalizers of its live objects it dropped); `Options()`. Unloads on delete. |
| `Fault`, `Guard(fn, context, fault)` | runs any `fn(context)` under the structured-exception guard: false, with the fault's code, instruction and touched address, when a hardware fault stopped it. |

```c4
include "scripting.ch"

int32 main()
{
	Scripting::Host* host = Scripting::Host::Open();
	Scripting::ScriptOptions options;
	options.name = Core::String("ai");
	options.AddFile("scripts/ai.cf");
	Scripting::Script* script = host->Compile(options);
	if (!script->Ok()) { printf("%s", script->Diagnostics()); return 1; }
	funcptr<int32(int32)> think = (funcptr<int32(int32)>)script->Address("Think");
	int32 move = think(3);
	script->Unload();
	return move;
}
```

Covered by `assets/ScriptTest/` (one host program per case, at `-O0` and `-O2`).

---

## 12. Platform (`Platform/`)

**Status:** **Implemented** (B40, D-63). The operating system through a small C API in the runtime
(`C4Runtime/PlatformRuntimeC.h`, one implementation file per area: `C4Runtime/Platform/*Win32.c`); Windows x64
today. Text is UTF-8 on every platform.

```c4
uint8* arena = (uint8*)Platform::Reserve(1 << 30);                     // 1 GiB of addresses, no memory yet
Platform::Commit((void*)arena, 1 << 20, Platform::Protection::ReadWrite);
Platform::ProcessorInfo cpu = Platform::GetProcessorInfo();             // cores, caches, line size
Platform::Library physics = Platform::Library("plugins/physics.dll");
funcptr<int32(int32)> step = (funcptr<int32(int32)>)physics.Find("Step");
Platform::CalendarTime now = Platform::LocalCalendar(Platform::UnixTimeNs());
```

### Virtual memory (`memory.ch`)

| Function | Meaning |
|---|---|
| `PageSize()`, `AllocationGranularity()` | 4 KiB pages; reservations start at a multiple of the granularity (64 KiB) |
| `Reserve(size)`, `ReserveAligned(size, alignment)` | addresses and no memory; the alignment is a power of two; null when `size <= 0` or there is no room |
| `Allocate(size, protection)` | a reservation committed whole |
| `Commit(address, size, protection)` | backs reserved pages with memory, zeroed when new (committed pages keep their bytes) and all with `protection`; false outside a reservation |
| `Decommit(address, size)` | gives the memory back; the addresses stay reserved and read as zero once committed again |
| `Release(base)` | gives back a whole reservation; `base` is what `Reserve`, `ReserveAligned` or `Allocate` returned (an inner address is refused) |
| `Protect(address, size, protection)` | changes the rights of committed pages |
| `Query(address, region)` | the run of pages holding `address`: `base`, `size`, `allocationBase`, `state` (`RegionState::Free`, `Reserved`, `Committed`) and `protection`; false outside the process's space |
| `FlushInstructionCache(address, size)` | after writing code, before running it |

`Protection` is `None`, `Read`, `ReadWrite`, `Execute`, `ReadExecute` or `ReadWriteExecute`: x64 has no write-only page,
so none can be asked for. Sizes are rounded out to whole pages and a size of zero or less is refused. The collector
never scans this memory: `GC::RegisterRegion` a range that holds managed pointers.

### The machine and the process (`system.ch`)

| Function | Meaning |
|---|---|
| `GetProcessorInfo()` | `ProcessorInfo { logicalProcessors, cores, packages, cacheLineBytes, l1DataBytes, l2Bytes, l3Bytes }` (one cache of each level; 0 when there is none) |
| `HasProcessorFeature(feature)` | whether the processor running the program has a `ProcessorFeature` (`Sse3`, `Ssse3`, `Sse41`, `Sse42`, `Popcnt`, `Pclmul`, `Aes`, `Avx`, `Avx2`, `Fma`, `F16c`, `Bmi1`, `Bmi2`, `Lzcnt`, `Sha`, `Avx512F`, `Avx512Dq`, `Avx512Bw`, `Avx512Vl`, `Vpclmulqdq`), for choosing a `[target]` function ([language 11 section 8](../language/11-vectors-and-swizzling.md#8-the-simd-target)); AVX and the features on its registers count only when the system saves those registers. Found once (CPUID, XGETBV) |
| `GetMemoryStatus()` | `MemoryStatus { totalPhysicalBytes, availablePhysicalBytes, commitLimitBytes, availableCommitBytes, totalVirtualBytes, availableVirtualBytes }` (the virtual ones are this process's address space) |
| `GetProcessMemory()` | `ProcessMemory { workingSetBytes, peakWorkingSetBytes, privateBytes, peakPrivateBytes }` |
| `ProcessId()` | |
| `ExecutablePath()` | the running program's file as an `FS::Path`; empty when the system cannot say |
| `TryGetEnvironmentVariable(name, value)` | false when it is not set; a variable set to `""` is set |
| `GetEnvironmentVariable(name)` | empty when it is not set |
| `SetEnvironmentVariable(name, value)`, `RemoveEnvironmentVariable(name)` | for this process and the processes it starts; removing an unset variable succeeds. A name is not empty and has no `=`; names and values must be UTF-8 |
| `RandomBytes(out, count)` | bytes from the system's cryptographically secure generator (keys, nonces, masks); false when it fails |
| `WriteOutput(text)`, `WriteError(text)` (`console.ch`) | the text to standard output or standard error, whole and flushed, in order with `printf`; false when the stream is closed or the write failed |

Setting a variable changes the system's block, not a copy the C runtime's `getenv` may hold.

### Libraries (`library.ch`)

`Platform::Library(path)` loads a library (a DLL) and what it needs; it frees it on delete and cannot be copied (move it
with `take`). A path with a folder (either separator) is loaded from there, and the libraries it needs are looked
for beside it first; a bare name is searched for as the system searches. `IsOpen()`; `Find(symbol)` is the
address of an exported function or variable, or null; `Close()` (idempotent); `Error()` says why it did not load,
in the system's words and with its code (`The specified module could not be found. (error 126)`, `(error 193)` for
a file that is no library). Opening one library twice gives the same module, which unloads when its last
`Library` closes.

### Source lines (`sourceline.ch`)

`Platform::SourceLineOf(code)` is where the instruction at a code address was written: `SourceLine { file, line }`
(`IsKnown()` when `line > 0`), as a profiler names a sampled frame (`GC::AllocSample::frames`) or a crash report a
return address. It reads the debug information of the module the address is in, a program built with `-g` or a
debug script's DLL (C4Runtime's `c4_source_line_of`, over the system's DbgHelp, loaded the first time it is asked
and asked one call at a time); a program without it, code a script compiled in memory, and data know no line.
`Reflection::FunctionContaining` names the function.

### Time (`time.ch`)

| Function | Meaning |
|---|---|
| `Ticks()`, `TickFrequency()` | the high-resolution counter: monotonic, the cheapest timestamp (profiling) |
| `UnixTimeNs()` | the wall clock: nanoseconds since 1970-01-01 UTC without leap seconds (an int64 spans 1677 to 2262); it jumps when the system's time is set |
| `LocalOffsetSeconds(unixNs)` | local time minus UTC at that instant, daylight saving time as it was then |
| `UtcCalendar(unixNs)`, `LocalCalendar(unixNs)`, `CalendarAtOffset(unixNs, utcOffsetSeconds)` | `CalendarTime { year, month, day, hour, minute, second, nanosecond, weekday, utcOffsetSeconds }` in the Gregorian calendar extended to every year |
| `ToUnixNs(time, unixNs)` | back to an instant, through the fields' own offset; fields out of range carry over (month 13, day 0, hour 24, a negative nanosecond); false when the instant does not fit an int64 |
| `IsLeapYear(year)`, `DaysInMonth(year, month)` | `DaysInMonth` is 0 for a month outside 1 to 12 |

Durations are measured with the prelude's `clock_now_ns()`, or a `Platform::Stopwatch` (`stopwatch.ch`): it
starts when made; `Restart()`, `ElapsedTicks()`, `Seconds()`, `Milliseconds()`, `Nanoseconds()` (exact for any
span an int64 holds) and `Lap()` (the seconds since the last lap, which starts the next: a frame's delta time). The calendar arithmetic is plain C4 (H. Hinnant's
days-from-civil algorithms); only the local offset comes from the system.

Covered by `Bedrock/Tests/platform_*_tests.cf` and, for faults on protected pages, `assets/IsolatedTest/Platform/`.

---

## 13. Networking (`Net/`)

**Status:** **Implemented** (B49, D-65): TCP and UDP over IPv4 and IPv6, name resolution and waiting on many sockets,
through a small C API in the runtime (`C4Runtime/NetRuntimeC.h`, `C4Runtime/Net/SocketsWin32.c` over Winsock 2).
HTTP is built on it (section 14).

```c4
Net::TcpListener server;
server.Listen(Net::Address::Loopback(0));                         // port 0: the system picks a free port
Net::TcpStream client = Net::TcpStream::Connect(server.LocalAddress(), 2000);
Net::TcpStream accepted = server.Accept(2000);                     // a closed stream when none comes in time
client.SendText(Core::StringView("ping"));
char[4] reply;
if (!accepted.ReceiveAll((void*)&reply[0], 4, 2000)) printf("%s\n", accepted.Error());
```

- **Addresses** (`address.ch`): `Net::Address` is a value - `family` (`Family::IPv4`, `IPv6`, or `None` for a zero
  address), `port`, 16 address `bytes` in network order and an IPv6 `scope` - built with `IPv4(a, b, c, d, port)`,
  `Loopback`, `LoopbackV6`, `Any`, `AnyV6`, `WithPort`; `IsLoopback()` (127.0.0.0/8, `::1`, or `::ffff:127.x`); compared with `==`, hashed (a map key) and printed with
  `ToString()` (`"127.0.0.1:80"`, `"[::1]:80"`, `"[fe80::1%3]:80"`). `Net::ParseAddress(text)` reads a numeric address
  with an optional port and never looks a name up; `Net::Resolve(host, port)` does, returning a `Resolution`
  (`addresses`, in the order the system prefers them, or `error`).
- **TCP** (`tcp.ch`): `TcpListener::Listen(address, backlog)`, `Accept(timeoutMs)`; `TcpStream::Connect(address,
  timeoutMs)`, `Send` (some bytes), `SendAll` / `SendText` (every byte), `Receive` (some bytes; 0 once the peer has
  closed its side), `ReceiveAll` (exactly a count), `SetNoDelay`, `Shutdown(Shutdown::Send / Receive / Both)`,
  `LocalAddress`, `PeerAddress`, `Close`. Each owns its socket and closes it on delete; neither can be copied (move
  one with `take()`), and a zero one is closed.
- **Local connections** (`local.ch`, B67): `LocalListener::Listen(name)`, `Accept(timeoutMs)` and
  `LocalStream::Connect(name, timeoutMs)`, `Send` / `SendAll` / `SendText`, `Receive`, `Close` - a channel between
  this user's processes on this machine, found by a name (a named pipe on Windows, over the runtime's
  `LocalRuntimeC.h`). No other machine and no other user can use one: remote clients are refused, the pipe's only
  access entry is the current user, and `Connect` refuses a listener that runs as another user (a name squatted
  first). A second `Listen` on a name someone listens on fails ("taken"), so a name is a single-instance lock. Four
  clients can connect before one is accepted; what is sent before a close still arrives. `Net::UserLocalName(base)`
  is `<base>-<the user's SID>`.
- **UDP** (`udp.ch`): `UdpSocket::Open(address)`, `SendTo(bytes, count, to)`, `ReceiveFrom(buffer, capacity, from,
  timeoutMs)` - a datagram arrives whole; one larger than the buffer is an error.
- **Waiting** (`poll.ch`): `Net::Poll(entries, timeoutMs)` over `PollEntry::For(socket, kReadable | kWritable)`
  entries (`TcpStream::Socket()`, `TcpListener::Socket()`, `UdpSocket::Socket()`); each entry's `ready` says what it
  found, including `kClosed` and `kFailed`. A listener is readable when a connection waits to be accepted.
- **Timeouts and errors**: a call that waits takes milliseconds, `0` to only check or `Net::kWaitForever`. `Send` and
  `Receive` return a byte count, `Net::kTimedOut` or `Net::kError`; a socket keeps why its last call failed
  (`Error()`: the system's message and code, empty after a success) and whether it gave up waiting (`TimedOut()`).
  `Connect` and `Accept` return a closed stream when they fail - the stream's (for `Connect`) or the listener's (for
  `Accept`) `Error()` and `TimedOut()` say why.
- Sockets are not inherited by processes the program starts. A listener takes its port exclusively. Windows answers
  a connection refused on this machine after about 2 seconds of retries.
- **The event loop** (`loop.ch`, B54, D-70): `Net::EventLoop` runs one thread's callbacks one at a time - a socket's
  (`Watch(socket, wanted, callback)`, called with what it is ready for each turn it is; `Change` and `Unwatch`), a
  timer's (`After(delayMs, ...)`, `Every(periodMs, ...)`, `Cancel`) and work posted from any thread (`Post`, run in
  the order posted). `Run` takes turns until `Stop` (safe from any thread); `RunOnce(timeoutMs)` takes one. A
  callback that throws ends the turn with the exception, the loop intact. Blocking work goes to another thread,
  which posts its result back. Underneath it is `Poll` plus a loopback datagram that ends a wait when work is posted.

```c4
Net::EventLoop loop;
loop.Watch(listener.Socket(), Net::kReadable, [&listener](int32 ready) { Serve(listener.Accept(0)); });
loop.Every(1000, []() { printf("tick\n"); });
loop.Run();
```

Covered by `Bedrock/Tests/net_tests.cf`: parsing and printing every address form and rejecting malformed ones, which
addresses are loopback,
resolution (also of a name that does not exist), a TCP round trip over IPv4 and IPv6, timeouts on accept, receive and
`ReceiveAll`, a refused connection, shutdown, a 4 MiB transfer from another thread, a port in use, closed and moved
streams, UDP datagrams (whole, timed out, too large), and `Poll` on listeners and streams. `local_tests.cf` (8) covers
local connections: both directions, a name as a single-instance lock, no listener (refused at once), timeouts, 40 KB
sent before a close arriving whole, four clients connecting before any is accepted, a client that leaves before it is
accepted, the name freed on close, refused names and the user's own name. `loop_tests.cf` covers the
event loop: timers in order, periodic timers cancelling themselves, 4000 pieces of work posted from 4 threads,
`Stop` from another thread, a callback that throws, watches paused and removed, and an echo server holding 1000
connections at once on one loop thread while a timer keeps running.

---

## 14. HTTP (`Http/`)

**Status:** **Implemented** (B50, D-66; WebSockets B51, D-67): HTTP/1.1 messages, a server that answers each
connection on a thread of its own, a client for one request at a time, and WebSockets both ways, all plain C4 over
`Net::`. Plain http and ws only: no TLS.

```c4
Http::Server server;
server.Listen(Net::Address::Loopback(8080));
server.Serve([](const Http::Request& request, Http::Response& response) {      // until server.Stop()
    if (request.path != "/status") { response.status = 404; return; }
    response.SetText(Core::StringView("application/json"), Core::StringView("{\"ok\":true}"));
});

Http::FetchResult got = Http::Get(Core::StringView("http://127.0.0.1:8080/status"), 2000);
if (got.ok) printf("%d\n", got.response.status);
```

- **Messages** (`message.ch`): `Request` (`method`, `target` as sent, `path` and `query` split at the `?` and not
  percent-decoded, `minorVersion`, `headers`, `body` bytes with `BodyText()`, and `peer`, the address the connection
  came from, which the server sets), `Response` (`status`, 200 unless set;
  `headers`; `body`; `SetText(contentType, text)`). `Headers` keeps the order headers came in and finds them without
  regard to case: `Find`, `Has`, `Set` (replaces every one of the name), `Add` (keeps the others, as Set-Cookie
  needs), `Remove`, `Count`, `At`. `ReasonPhrase(status)`.
- **Parsing** (`parse.ch`): pure functions over bytes the server and the client share - `ParseRequestHead`,
  `ParseResponseHead` (Complete with the head's length, Incomplete, TooLarge, or Malformed with why and the status a
  server answers: 400, 431, 505), `RequestFraming` / `ResponseFraming` (`Content-Length`, chunked, none, or until
  close; a bad or conflicting length, or both framings at once, is Malformed; another transfer coding Unsupported)
  and `ChunkedDecoder`, fed bytes as they arrive. Lines may end in CRLF or LF; empty lines before a request line are
  skipped; folded header lines, control characters in values and malformed tokens are refused.
- **Server** (`server.ch`): `Listen(address)`, `Serve(handler)` on the calling thread until `Stop()` (safe from any
  thread, also from a handler: no new connection is accepted, each one ends once it has answered its request, and
  Serve returns when they have), `SetLimits(ServerLimits { maxHeadBytes, maxBodyBytes, idleTimeoutMs,
  requestTimeoutMs })` (64 KiB, 16 MiB, 30 s, 30 s by default). Each connection reads requests one after another
  (keep-alive; pipelined requests are answered in order) and the handler runs on several threads at once. The server
  writes the framing - `Content-Length`, `Connection: close` when it closes after a response - and answers
  `Expect: 100-continue` before reading the body; HEAD gets the head only. It refuses with 400 (malformed), 408 (too
  slow), 413 (body over the limit), 431 (head over the limit), 501 (another transfer coding) and 505 (another
  version); a handler that throws answers 500.
- **Client** (`client.ch`): `Fetch(address, request, timeoutMs)` sends one request on a connection of its own and
  reads the response (interim 1xx ones skipped; a `Content-Length`, chunked or until-close body); `Get(url,
  timeoutMs)` and `Post(url, contentType, body, timeoutMs)` take an `http://host[:port]/path?query` URL
  (`ParseUrl`; an IPv6 host in brackets). A `FetchResult` is `ok` with the `response`, or not, with `error`.

- **Content codings** (`coding.ch`; RFC 9110 section 8.4): `CompressResponse(request, response, level)` gzips the
  body (Compression section 19) when the request's Accept-Encoding takes gzip (`AcceptsCoding`: listed, or `*`, without
  q=0), the body is `kCompressFromBytes` (1 KiB) or more, its type compresses (`CompressibleType`: text, JSON,
  JavaScript, XML, SVG, WebAssembly, `+json` and `+xml`), it has no coding yet, and the result is smaller; it sets
  Content-Encoding and Vary. `DecodeBody(headers, body, why)` decodes a received gzip or deflate body (a zlib stream,
  or raw DEFLATE as some servers send) in place and drops the header; false for another coding or a damaged body,
  which it leaves as it came.

- **WebSockets** (`websocket.ch`, `frames.ch`; RFC 6455): in a handler, `Http::AcceptWebSocket(request, response,
  session)` answers the handshake (101; 400 for a request that is not one, 426 for another version) and the session
  runs with the connection's `WebSocket` on the connection's thread once the 101 is out; the connection closes when
  the session returns, and a server that stops closes its sessions with 1001. A client connects with
  `WebSocket::Connect("ws://host:port/path", timeoutMs)`. `Receive(message, timeoutMs)` waits for a whole message
  (`kind` Text or Binary, `data`, `Text()`): fragments are joined, pings answered, the peer's close answered, and it
  is false when the connection closed (`CloseCode()`, `CloseReason()`), failed (`Error()`) or the wait timed out
  (`TimedOut()`; still open). `LastReceivedNs()` is when bytes last came from the peer, a pong included, so a server
  can tell a quiet client from a gone one. `SendText`, `SendBinary`, `Ping` and `Close(code, reason)` are safe from any thread
  while another receives - a server can push from anywhere. Bad frames close the connection with the RFC's code:
  1002 (an unmasked client frame, reserved bits, an unknown opcode, a length not in its shortest form, a fragmented
  or long control frame, a stray continuation), 1007 (text that is not UTF-8), 1009 (over `SetMaxMessageBytes`, 16
  MiB by default). The frame layer is pure functions too (`ParseFrame`, `WriteFrame`, `ReadClosePayload`,
  `ClosePayload`, `WebSocketAccept`, `IsWebSocketRequest`). `Response::takeover` is the general hook underneath:
  after a 101 the server hands it the connection.

Covered by `Bedrock/Tests/http_tests.cf` (27 tests): heads (every malformed form, the limits, CRLF or LF, absolute
targets), framing, chunked bodies fed whole and a byte at a time, URLs, headers; the server's GET and POST, the
client's address a handler sees, three
pipelined requests on one connection, a chunked request, HEAD, Expect: 100-continue, HTTP/1.0, the refusals, a
handler that throws, 8 concurrent clients and Stop from a handler; the client's chunked, until-close and interim
responses and its failures. `websocket_tests.cf` (16): frames at every length form, masked and not, every rule a
frame can break, close payloads, RFC 6455's own handshake example, and on loopback the echo of text and 100 KB of
binary, fragments with a ping between them, the 1002 / 1007 / 1009 closes, a close from either side, the server
stopping, 100 pushes from 4 threads, timeouts and failed connections. `encoding_tests.cf` covers SHA-1 (FIPS 180
vectors, a million bytes, pieces against whole) and Base64 (RFC 4648 vectors, every byte, malformed text).

---

## 15. Processes (`Process/`)

**Status:** **Implemented** (B52, D-68): programs started with pipes or under a terminal, each in a job object, over
a small C API in the runtime (`C4Runtime/ProcessRuntimeC.h`, `C4Runtime/Process/*Win32.c`).

```c4
Process::StartInfo info;
info.program = Core::String("git");                // a path, or a name found as the system finds programs
info.arguments.Push(Core::String("log"));
info.arguments.Push(Core::String("-1"));
Process::Output git = Process::Run(info, 10000);   // stdout and stderr captured; ended after 10 s
if (git.exitCode == 0) printf("%s", git.output.CStr());
```

- **Starting** (`child.ch`): `Process::Child::Start(info)` gives an open `Child`, or a closed one with `Error()` (no
  such program, no such folder, a batch file). A bare program name is looked for as the system looks (the
  program's folder, the current folder, the system folders, `PATH`), with `.exe` added when it has no extension.
  Each argument reaches the child as written: it is quoted for `CommandLineToArgvW` and the C runtime. Batch files
  are refused as programs, because `cmd.exe` reads arguments its own way: set `shellCommand` instead of `program`
  and `arguments` to a command line written for the shell, which runs it as written (`cmd.exe /d /s /c "..."`,
  `%ComSpec%` or the system folder's `cmd.exe`): `info.shellCommand = Core::String("nuke build \"My Game\"");`.
  `environment` holds changes: `"NAME=value"` sets a variable, `"NAME"` removes one, and names compare as the
  system compares them (ignoring case on Windows); with `inheritEnvironment = false` they are the whole environment.
  `workingDirectory` empty is this process's.
- **Streams:** each of `input`, `output` and `errors` is `Redirect::Inherit` (this process's own), `Pipe`, `Null`,
  or, for errors only, `ToOutput` (the same pipe as the output). Only the child's ends of its own pipes are
  inherited, so children started at once from several threads never hold each other's pipes open. `Write`,
  `WriteAll`, `WriteText` and `CloseInput` feed the input. `Read(stream, ...)` reads one stream, and
  `ReadAny(buffer, capacity, from, timeoutMs)` whichever of output and errors has bytes first, so one thread can
  follow both. A stream that ends is closed, and `HasOutput()` turns false once both have. A stream ends (a read
  gives 0) when the child and everything it started have closed it.
- **The child's life:** `Wait(timeoutMs)` gives the exit code (0 to 4294967295) or `kTimedOut`. The child runs in a
  job object: `Kill()` ends it and every process it started (their exit code is 1), and so does closing or
  deleting the `Child` unless `killOnClose` is false. A `Child` owns its handles and cannot be copied (move it with
  `take()`). One thread may read while others write, `Wait` or `Kill`; `Close` runs alone.
- **Terminals:** with `terminal = true` the child runs under a pseudo console (ConPTY, Windows 10 1809 or later) of
  `columns` x `rows`: `Write` is its keyboard, `Read(Stream::Output, ...)` its screen as VT sequences, and
  `Resize(columns, rows)` changes its size. Its output does not end when the child does, only when the `Child`
  closes, so wait for the child itself with `Wait`.
- **Run** (`run.ch`): `Process::Run(info, timeoutMs)` runs a program to its end with its input from nowhere and its
  stdout and stderr captured into an `Output` (`exitCode`, `output`, `errors`, `error`); a program still running at
  the timeout is ended with everything it started, and `exitCode` is `kTimedOut`.
- **Usage** (`usage.ch`): `Process::GetUsage(id, usage)` reads what a running process uses, found by the system's id
  (`Child::Id()`, `Platform::ProcessId()`, or a program another tool started): `Usage { cpuTimeNs, workingSetBytes,
  privateBytes, peakPrivateBytes }`, its processor time in user and kernel mode so far and its memory now. false, with
  `usage` zeroed, when there is no such process (it ended, even while something still holds it) or the system will not say; it asks
  only for limited query rights. A watcher works out CPU use from two readings: the time's growth over the wall
  clock's, divided by `Platform::GetProcessorInfo().logicalProcessors` for a share of the whole machine.

Covered by `Bedrock/Tests/process_tests.cf` (20 tests) against the system's own programs (`cmd`, `sort`, `findstr`,
`ping`, `tasklist`): output and errors apart and together, 5000 lines out and 600 KB in (far past a pipe's buffer),
exit codes up to 4294967295, arguments with spaces, quotes and trailing backslashes as the child reads them, the
environment changed (set, replaced in another case, removed, inherited) and replaced, the working folder, `ReadAny`
until both streams end, timeouts on reads, waits and `Run`, `Kill` ending `cmd` and the `ping` it started, closing
with and without `killOnClose`, 32 children from 8 threads at once, misuse and failures, a terminal: a shell
typed to, resized, and exiting with the code it was told, and usage: this process's memory and its processor time
growing while it works, a child read by its id, and an ended one gone.

---

## 16. Cryptography (`Crypto/`)

**Status:** **Implemented** (B66). Plain C4, checked against the standards' own test vectors. No ciphers and no TLS.

| Header | Gives |
|---|---|
| `sha256.ch` | `Crypto::Sha256(bytes, count)` or a `Sha256Builder` fed in pieces (`Add`, then `Finish` once): a `Sha256Digest` (`bytes[32]`, `ToHex()`, `==` and `!=` in constant time) - content hashes, stored digests of secrets |
| `hmac.ch` | `Crypto::HmacSha256(key, keyCount, message, count)` (RFC 2104): a tag only a holder of the key can make |
| `blake2b.ch` | `Crypto::Blake2b(out, digestBytes, bytes, count)` (RFC 7693) with a digest of 1 to 64 bytes, or a `Blake2bBuilder` (`Start(digestBytes, key, keyCount)`, false for a size or key out of range; `Add`; `Finish(out)`) |
| `argon2.ch` | `Crypto::Argon2(params, input, tag)` (RFC 9106): `Argon2Params` (`type` D, I or Id, `memoryKiB`, `passes`, `lanes`, `tagBytes`; `DefaultArgon2Params()` is RFC 9106's second recommendation, Argon2id at 64 MiB, 3 passes, 4 lanes, 32 bytes) and `Argon2Input` (password, salt of at least 8 bytes, optional secret and associated data). The lanes run one after another. False for costs out of range or no memory. |
| `password.ch` | `Crypto::HashPassword(password, params)`: a PHC string, `$argon2id$v=19$m=65536,t=3,p=4$<salt>$<tag>`, with a fresh 16-byte salt; `Crypto::VerifyPassword(password, encoded)` checks one (any Argon2 type; false for a malformed string or one asking for more than 4 GiB) |
| `compare.ch` | `Crypto::ConstantTimeEquals(a, b, count)`: takes the same time wherever the bytes differ, for checking secrets |

Argon2id at the default costs takes about 110 ms built with `-O2`, so a server can check a password per login but an
attacker pays the same for each guess. Covered by `Bedrock/Tests/crypto_tests.cf`: SHA-256 (FIPS 180 vectors, a
million bytes, pieces against whole around the padding), HMAC (RFC 4231 cases 1, 2 and 6), Blake2b (RFC 7693, keyed,
32-byte and 20-byte digests, every block boundary a byte at a time, the refusals), Argon2 (RFC 9106's vectors for all
three types, the reference tool's example string at 64 MiB, the refusals) and password strings (a round trip, fresh
salts, ten malformed strings, a changed tag).

---

## 17. Profiling (`Profile/`, `include "profile.ch"`)

**Status:** **Implemented** (IED docs/16): the client of the IED Trace Protocol. A program run with `IED_PROFILER`
set streams its zones, frames, plots, messages, thread names, tasks, locks, allocations and collections to the IED profiler
(the IED Profile task sets it to `127.0.0.1:<port>;token=<t>`) or, with `file:<path>`, to a trace file; without it each
call costs one relaxed load. Opt-in like `math.ch`: `bedrock.ch` does not include it.

```c4
include "profile.ch"

void Update(World& world) {
	PROFILE_ZONE("Update");                                  // to the end of the scope, with __FILE__ and __LINE__
	Profile::Zone step(Core::StringView("Physics"));          // a zone named at run time
	step.Text(Core::StringView("bodies=2048"));
	Profile::Plot(Core::StringView("Entities"), (double)world.Count());
}
// once per presented frame
Profile::Frame();
```

| API | What it records |
|---|---|
| `Site`, `Zone(Site&)`, `Zone(StringView)`, `Zone::Text`, `Zone::Value` | a zone of the calling thread from its creation to its scope's end, with what the thread allocated in it (`GC::ThreadAllocated` read as it begins and ends: objects and bytes, and the native memory it took - containers' buffers growing, raw blocks - as `ZoneNative`, and gave back as `ZoneNativeFreed`; its nested zones' included, what the profiler's own work took meanwhile left out, sent only when it allocated); a static `Site` is registered once per session, a run-time name looked up each time; `PROFILE_ZONE(name)` declares both |
| `Frame()`, `Frame(set)`, `FrameBegin(set)`, `FrameEnd(set)` | the end of a frame of the default set or a named one, with the process's allocations so far (`GC::Allocated`) at the same time, so a frame's are the difference between its marks; discontinuous frames |
| `Plot(name, value)`, `Message(text, severity)` | a value over time; a log line (`Severity::Trace` .. `Fatal`) |
| `SetThreadName(name)` | the calling thread's name from now on (call it again when a pooled thread takes another role: the profiler keeps the history); the system's thread is named too (`Thread::SetCurrentName`), and a thread the system already knows by a name starts with it |
| `TaskBegin(name, parent) -> id`, `TaskEnd`, `TaskSuspend`, `TaskResume`, `TaskScope` | units of work the profiler's Tasks view groups by name wherever they ran |
| `Mutex`, `MutexGuard`, `PROFILE_MUTEX(variable, name)` | a `Threading::Mutex` whose waits and holds the profiler shows (`Lock`, `TryLock`, `Unlock`; a free mutex is taken without recording a wait); the macro gives it this file and line |
| `LockSite`, `LockWait(site, shared)`, `LockObtained`, `LockReleased` | a lock of the program's own: its name and place, registered once per session, and the calling thread's wait, hold and release (`shared` for a reader of a reader-writer lock) |
| `Alloc(ptr, size, pool)`, `Free(ptr, pool)`, `PoolName`, `PoolReset` | a pool of the program's own (sent in one global order, so an address reused by another thread is never confused) |
| `IsActive()`, `Start(target, problem)`, `Stop()` | for tools and tests; a program run by IED starts from its environment on its first call |

While a session runs, every `JobSystem` job is a task by itself, named as it was submitted ("Job" without a name), on
the worker that ran it (`Threading::SetJobObserver`, installed as the session starts and removed as it stops).

Collections are reported from the runtime's collection callback, a `GcBegin`/`GcEnd` pair for each time the collection
stopped the program (`GC::LastPauses`: one for `GC::Collect`, several for a concurrent cycle). Ten times a second the
GC heap's size, the process's private bytes (`Platform::GetProcessMemory`) and the runtime's native memory in use
(`GC::Stats.nativeBytes`: containers' buffers and raw blocks, outside the GC heap) are plotted, with the process's
allocations so far. Each thread's event buffer starts with 16 KiB reserved and the flusher reserves its room again as it
takes the events, and what the profiler's own work allocates on a thread (names, sites, events) is counted apart, so a
zone's counts are the program's alone. While a session runs
the collector also takes a census after each collection (`GC::SetCensus`), sent as a `HeapCensus` of the live objects and the native memory they hold (`HeapCensusTypeNative`) by
type (those made before the session, by native code or in a `Pool` as "(type not known)"), and samples about one
allocation in every 64 KiB (`GC::SetAllocSampling`; a rate the program set is kept): the flusher sends each as a
`Sample` of its thread standing for that many bytes, with its call stack (`CallstackDef`) and each address named by the
reflected function holding it and the source line of its call (`Reflection::FunctionContaining`,
`Platform::SourceLineOf` for a program built with -g; `SymbolDef`; native code stays unnamed). Both are put
back as they were when the session stops (`Profile/collector.cf`). The census makes each `new` dearer while it is on
(about 16 ns in IED's LeakyC4 demo at -O2, the median of a zone of 300 allocations with it on and off). Events are stamped with the processor's time stamp counter (its
frequency measured against `Platform::Ticks` at start; calibration pairs every second let the profiler correct it),
written by each thread into a buffer of its own under an uncontended lock, and sent by a flusher thread every 10 ms -
each thread's events taken first, then the global buffer's, sent first, so a definition always precedes its first use. A
failed send ends the session. One session runs per process, kept in globals (the module's documented exception to the
no-globals convention). `-D PROFILE=0` compiles the `PROFILE_*` macros to nothing; direct calls stay and cost one load.

Covered by `Bedrock/Tests/profile_tests.cf` (9 tests): a session to a file read back event by event - the preamble and
`Hello`, definitions before their use, zones on two threads with times that only grow across chunks, frames, plots,
messages, renames, tasks, a pool, a collection and `Bye`; a thread named by the system starting with that name, a
`GcBegin`/`GcEnd` pair for each stop of a collection, inside the session; a `Profile::Mutex` one thread waits for while
another holds it (its definition, the wait, both holds); zones that say what they allocated (exactly a hundred objects,
nested zones' included and the profiler's naming of them left out, 64 KiB of native memory from a zone that grew an array
and none from the others, given back as its scope ended, nothing from a zone that allocated nothing) and frames with the
process's count; a census after a collection (a hundred live objects by their type's name, the native memory ten holders'
buffers make theirs and none a blob's, what came before under a name of its own, the census off again after); allocations sampled with the function that made them and, built with -g, the line of the call (on the allocating thread, each for 64 KiB, its stack
defined first, sampling off again after); `JobSystem` jobs as
tasks of their names on the named workers, and none once the session stopped; and nothing recorded without a session, an unreachable or malformed target refused.

---

## 18. GPU capture (`Gpu/`, `include "gpu.ch"`)

**Status:** **Implemented** (IED docs/22): the client of IED's GPU debugger. A program started by IED's Launch for
Capture finds the capture library in `FLASHBANG_DLL`; `Gpu::Capture` loads it, and a D3D12 program hands it its device
and DXGI factory right after making them, then keeps using the proxies it gets back, which record what the program
does for the frames IED (or the program) asks for. A Vulkan program needs none of this: the loader brings the capture
layer in. Without the library - a program not started for capture, or one built for shipping - every call gives back
what it was given and records nothing. Opt-in like `math.ch`: `bedrock.ch` does not include it.

```c4
include "gpu.ch"

Gpu::Capture capture;                                      // keep it as long as the device
if (!capture.Load()) printf("%s
", capture.Error());     // FLASHBANG_DLL is not set: the program runs alone
D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), &raw);
ID3D12Device* device = (ID3D12Device*)capture.WrapD3D12Device(raw);
// a program with no swap chain bounds its frames itself
capture.StartFrame();
Render(device);
capture.EndFrame();
```

| API | What it does |
|---|---|
| `Load()`, `Load(path)`, `Unload()`, `IsLoaded()`, `Error()`, `Version()` | the capture library from `FLASHBANG_DLL` or a path; false, with why, when it is not set, does not load (the system's reason) or is not the capture library; it frees the library on delete and cannot be copied (move it with `take`) |
| `WrapD3D12Device(device)`, `WrapDxgiFactory(factory)` | the object to keep using: a proxy that takes the object's reference (any interface version), or the object itself when not loaded; swap chains a wrapped factory makes are captured, their `Present` bounding frames |
| `TriggerCapture(frames)`, `IsCapturing()` | capture the next `frames` frames (IED's Capture Next Frame sends the same through the library's control pipe) |
| `StartFrame()`, `EndFrame()` | frame bounds for a program that does not present: each `StartFrame` is a frame, captured when one was triggered |
| `SetComment(text)`, `Annotate(key, value)`, `SetCaptureFolder(folder)` | a note kept with the next capture, a key and value recorded with it, where captures go instead of `FLASHBANG_CAPTURE_DIR` |
| `CaptureCount()`, `CapturePath(index)` | the captures this process has written |

Covered by `Bedrock/Tests/gpu_tests.cf` (4 tests): without the library every call passes through and records nothing;
loading from `FLASHBANG_DLL` unset, empty or naming nothing; a missing library and one without the capture exports
refused with why. The real library is covered by IED's `plugins/gpu/native/tests/test_flashbang.py`, which builds the
C4 sample `plugins/gpu/samples/c4` with the toolchain and captures one triggered frame of it through `Gpu::Capture`.

---

## 19. Compression (`Compression/`)

**Status:** **Implemented**. Plain C4, checked against streams zlib wrote and read back by zlib.

| Header | Gives |
|---|---|
| `inflate.ch` | `Compression::Inflate(data, size, out)`: a raw DEFLATE stream (RFC 1951) appended to `out`; an `InflateResult` (`ok`, `consumed`: the input bytes the stream took, `error`: why it failed) |
| `deflate.ch` | `Compression::Deflate(data, size, level, out)`: `size` bytes as a raw DEFLATE stream appended to `out`. Levels as zlib's: `kStoreLevel` (0) stores, `kFastestLevel` (1) to `kBestLevel` (9), `kDefaultLevel` (6); each block is stored, fixed-coded or given codes of its own, whichever is smallest |
| `lz4.ch` | LZ4 (lz4 1.9's formats): byte-oriented LZ77 with no entropy stage, for data read often and fast (streamed assets). `Lz4CompressBlock(data, size, out, acceleration = 1)` appends one block (`Lz4Bound(size)` at most; a higher `acceleration` skips faster through data that does not match), `Lz4DecompressBlock(data, size, limit, out, history = 0)` appends at most `limit` bytes (a match may reach back `history` bytes into `out`); `Lz4CompressFrame(data, size, out, acceleration = 1)` writes a frame (independent 4 MiB blocks, the content size and the content's XXH32; a block that would not shrink is stored), `Lz4DecompressFrame(data, size, out)` reads any frame (every block size, independent or linked blocks, block and content checksums, skippable frames before it; `consumed` says where a next one starts). Both return an `InflateResult` |
| `xxhash.ch` | `Xxh32(data, size, seed = 0)`: XXH32, the LZ4 frame's checksum (fast and well spread, not for secrets) |
| `zlib.ch` | `ZlibCompress` / `ZlibDecompress` (RFC 1950: a two-byte header, the DEFLATE stream, the Adler-32 of the data), `GzipCompress` / `GzipDecompress` (RFC 1952: one member; reading takes the optional name, comment, extra field and header CRC, and `consumed` says where a next member starts), `Adler32(data, count)` |

Reading rejects what zlib rejects: a reserved block type, a stored length its complement does not match, an
over-subscribed code, a distance back past the start of the output, a stream cut short, and (zlib, gzip) a damaged
header, a preset dictionary, a checksum or size that does not match. Nothing is read past `size`.

How fast, on one core of the development machine against zlib-ng 2.2.4 (Python's zlib, itself faster than zlib):
reading runs at 850 MB/s on text and 1.8 GB/s on match-heavy data (zlib-ng: 650 MB/s and 960 MB/s). Writing is
level with zlib-ng at the middle levels, ahead on data that compresses little (incompressible data at level 1:
365 MB/s against 103; past 64 searches that find nothing, the fast levels search every second position, then every
third, as LZ4 does) and at level 9, and behind at the fastest levels on text, where zlib-ng codes with the fixed codes
only (its level 1 is a fifth larger). Adler-32 runs 16 bytes a step, 32 with AVX2 where the processor has it (23 GB/s).
Levels 1 and 2 find matches in a hash table of two positions a bucket, the others along hash chains, both keyed by
four bytes (zlib-ng's choice).

LZ4 on the same core, on 5 MB of this repository's code and docs: compressing at about 500 MB/s to 43% (acceleration
8: about 650 MB/s, 56%), reading at 2.5-3 GB/s; the reader decodes the common sequence (under 15 literals, a match of
at most 18 bytes from at least 8 back) in fixed-size copies and a short period in 8-byte steps, the writer hashes 5
bytes as lz4 does on 64-bit processors. Its blocks were checked against an independent decoder written from the
format's description. Reading rejects a match before the data, an offset of zero, a block that ends in a match or
runs past its limit, and (frames) another version, a bad block size, a dictionary, a header, block or content checksum
or a content size that does not match, and a frame cut short; nothing is read past `size`.

Covered by `Bedrock/Tests/compression_tests.cf`: streams zlib wrote (stored, fixed and dynamic blocks, a zlib stream,
a gzip member with a name), every cut of a stream, damaged blocks, checksums, sizes and headers, a gzip member with an
extra field, a comment and a header CRC, the Adler-32 check values, every level's output of seven inputs (nothing, one
byte, sentences, runs, noise, a mix, matches 32 KiB back) read back raw and wrapped, and the match finder's slide; and by `Bedrock/Tests/lz4_tests.cf` (11 tests): XXH32's published values, blocks and frames
of five kinds of data at sizes from nothing to past one 4 MiB block, acceleration, hand-made blocks (literals only, an
overlapping match, a long literal run), linked and independent blocks, a stored block with its checksum, the reference
lz4's empty frame, skippable and concatenated frames, and every cut and a changed byte of blocks and frames.

---

## 20. Images (`Imaging/`)

**Status:** **Implemented**. Plain C4 over Compression (section 19), checked against PNG files of every kind and read
back by PIL.

| Header | Gives |
|---|---|
| `png.ch` | `Imaging::DecodePng(data, size, image)`: a PNG file's image as an `Rgba` (`width`, `height`, `pixels`: four bytes a pixel, rows top to bottom); a `ReadResult` (`ok`, `error`). `Imaging::EncodePng(image, level, out)`: the image as a PNG file appended to `out`, at a Compression level |

Reading takes every color type (gray, RGB, a palette, gray with alpha, RGBA) at every bit depth PNG has (1, 2, 4,
8 and 16), a palette's and a gray or RGB image's tRNS transparency, and Adam7 interlacing; low depths stretch to the
full byte, 16-bit samples keep their high byte. Every chunk's CRC is checked; ancillary chunks are skipped and an
unknown critical one fails, as do a damaged or cut file, a depth PNG lacks, a palette index past the palette and an
image of more than `kMostPixels` (2^28). Writing makes RGB when every pixel is opaque and RGBA otherwise, each row with
the filter whose bytes sum least (libpng's heuristic). Four-byte pixels unfilter a pixel at a time, their channels in
SIMD lanes. A 1920x1080 RGBA screenshot reads in about 23 ms (PIL: 28) and writes in about 118 ms at level 5, 2%
smaller than PIL's level 6 (120 ms).

Covered by `Bedrock/Tests/png_tests.cf`: eight files written by hand (gray at 1 and 16 bits, RGB at 8 and 16, palettes
of 2 and 8 bits, gray with alpha, RGBA at 16 bits, interlaced or not, with tRNS), each written back at four levels,
RGB chosen for opaque images, and damaged files (signature, CRC, every cut, no IHDR first, too many pixels, a depth
PNG lacks, a palette index past the palette).

---

## 21. Tests

`Bedrock/Tests/*_tests.cf` use the built-in `[test]` framework: 77 files, 678 tests (array, list, initlist,
container, container_alignment, element_lifetime, path, directory, json_reader, yaml_reader, hashmap, stringview, string, stringbuilder, format, file, dom, binary, reflection,
reflection_invoke, reflection_annotation, reflection_layout, namespaced_template, overload, int_width, math_scalar, math_vector,
math_matrix, math_quaternion, math_transform, math_batch, threading, threading_jobs, platform_memory, platform_system,
platform_library, platform_time, net, loop, http, websocket, encoding, process, file_info, watch, container_move, regex, reflect_value, utf16, jsonstream, gc_pool, crypto, local, schema, number_text, hashset, bitset, random, priorityqueue, lrucache, crc32, uuid, stopwatch, sort, profile, gpu, gc, buffer, compression, http_coding, png, gc_alloc, gc_census, reflection_address, lz4, native_bytes, platform_source_line). They also pass built for `-O2 -march=x86-64-v3` and `-march=native`. Run them with:

```bat
bin\Development-windows-x64\C4\C4.exe Bedrock\Tests -o assets\build-output\bedrock\tests.exe -test
assets\build-output\bedrock\tests.exe
```

The exit code is the number of failed assertions. The `GC` namespace is covered by
`assets/IsolatedTest/MemoryGC/` and the gc, gc_pool, gc_alloc and gc_census tests. Performance against the C++ standard library is `Benchmarks/bedrock/`
([test suites 8](../tools/05-test-suites.md#8-benchmarks)). Thin areas: reflection groups.

---

## 22. Missing versus the plan

From LongTermPlan.txt items 3 and 7 and roadmap item 3:

| Item | Status |
|---|---|
| Array (`std::vector`) | **Implemented** |
| Hash map that uses `operator_hash` when the key type defines `[operator("hash")]` | **Implemented** (`KeyTraits<K>`) |
| UTF-8 string replacing a built-in string | **Implemented** (`StringView`, `String`, `StringBuilder`) |
| Regular expressions | **Implemented** (section 4, B55): RE2 syntax, linear time |
| Stack and Queue, with range-for | **Implemented** |
| Consistent index type across containers | **Implemented** (`int64`) |
| List (linked list) | **Implemented** |
| Math library in `Math::`: vectors with swizzling, matrices, quaternions, the math functions, left-handed, 0-1 NDC, parity with GLM | **Implemented** (section 9), opt-in through `include "math.ch"` |
| Math with explicit SSE/AVX intrinsics and wide (AVX) batches of vectors | **Implemented** (section 9: the vector and matrix functions on the SIMD intrinsics, `batch.ch`) |
| ObjectDescription with in-memory JSON and YAML read and write | **Implemented** (one YAML document; no anchors or tags) |
| Objects to and from JSON and YAML through reflection, strings, arrays, maps and enums included | **Implemented** (section 6, B56) |
| Streaming reads (decode a large file while it is still being read) | **Implemented** (sections 5 and 6, B62): `FS::ChunkReader`, `JsonStream`, `JsonFileStream`, `BinaryStream` |
| Paths, directory listing, create, remove, rename | **Implemented** |
| File information and times, copies of files and trees, absolute and canonical paths, watching a directory | **Implemented** (section 5, B53) |
| `Core::InitList<T>` | **Implemented** |
| Sockets / networking | **Implemented** (section 13, B49), with an event loop and timers (B54) |
| HTTP server | **Implemented** (section 14, B50), with WebSockets (B51) |
| Child processes: pipes, exit codes, environment, killing a tree, terminals | **Implemented** (section 15, B52) |
| Threading primitives (join, promise, condition variable, work stealing) | **Implemented** (section 10, B29) |
| `Platform::`: virtual memory, system and process information, environment variables, libraries, wall-clock time | **Implemented** (section 12, B40) |
