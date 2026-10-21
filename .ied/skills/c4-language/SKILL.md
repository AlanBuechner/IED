---
name: c4-language
description: Overview and everyday reference for the C4 language - program-model compilation, type-specifier-on-type syntax, [operator(...)] constructors/destructors/operators with canonical names, extension methods (extends), funcptr/lambdas, packed SIMD vectors + swizzling, unions/templates/interfaces/visibility, control-flow extras, builtins and macros. Entry point is int32 main(). The full reference ships in the toolchain at docs/; deep dives live in c4-testing, c4-reflection and c4-garbage-collector skills.
triggers:
  - "writing C4 code"
  - "C4 syntax overview"
  - ".cf .ch files include program model"
  - "int32 main entry point"
  - "operator create delete annotation canonical names"
  - "funcptr lambda return type"
  - "array pointer const reference union packed swizzle SSE"
  - "extension method extends operator overload rule"
  - "class struct interface virtual visibility template"
---

# C4 language (overview)

C4 is a mostly C++-like compiled systems language with syntax changes and extra features; beyond those it behaves like C++. Source files are `.cf` (definitions) and `.ch` (headers), included with `include "path.ch"` - **not** the C textual preprocessor. The program entry point at file scope is:

    int32 main() { return 0; }

The toolchain ships the full reference at `docs/` (`01-what-is-c4.md`, `language/01..16`, `library/bedrock.md`, `runtime/*`, `tools/01..03`) - consult it when this overview is not enough.

Deep subsystems have their own skills - this overview only needs to know they exist:
- **c4-testing** - built-in `[test]` unit testing (annotations, fixtures, assertions).
- **c4-reflection** - runtime reflection registry and the Bedrock `Reflection::` wrappers.
- **c4-garbage-collector** - Go-style stop-the-world GC internals and engine hooks.

## Program model & includes
All source files of a project compile together as one program (no per-file object/link step):
- Every file that includes a header parses the exact same header content, so defining something before an include does **not** change how that header is parsed.
- No forward declarations of types outside function bodies - names register in order; inside a function body you may still need one.
- A `static` variable declared in a `.ch` is fully defined there (the header IS a source file) - no separate definition needed in a `.cf`.
- Functions implemented elsewhere still need a forward declaration so includers see them. Templated functions can live in the `.cf`, not just the header.

### Macros / preprocessor info carried through includes
Includes are not C-preprocessor, but they **do carry macro (preprocessor) state** down the include chain - `#define`d macros from an included base header are visible to the includer:

    // Base.ch (included first): #define MACRO_CONST 42   /  #define DOUBLE(x) ((x)*2)
    int32 a = MACRO_CONST;      // object-like macro in expression position -> 42
    MACRO_TYPE t = MACRO_CONST; // also usable in type position (MACRO_TYPE == int32)
    b = DOUBLE(10);             // function-like macro, composes with other macros

Object-like and function-like `#define`s both work and can compose. Because of the program model, a local `#define` does not retroactively re-parse an already-included header - it affects code after it in the current file's parse.

## Types & declarations
Arrays, pointer/const placement are part of the TYPE, not the variable:

    const int32[10] arr;      // array of 10 const ints   (C++ would be: const int arr[10])
    const int32*[5] arr;      // array of 5 mutable pointers to const ints
    const int32 const*[5] arr; // array of 5 const pointers to const ints
    const int32* i;           // mutable pointer to a const int
    int32 const* i;           // const pointer to a mutable int
    // A const qualifies the layer after it; `int32* const p` and `int32 const& r` are errors.
    // Const methods: `int32 Get() const`; a const object / const T& / const T* calls only those.
    int32& i;                 // reference to a mutable int (like C++); &i is the referent's address
    struct View { int32& r; } // reference field: bound by a braced entry, its initializer or an assignment in its
                              // object's create; every other use goes through it; it owns nothing
    p->x; o.x;                // '->' through a pointer, '.' on an object or reference; the other spelling is an error (`this` takes both)

**Strict aliasing (C++'s rule, D-64):** memory is read and written only as its own type (all integers of one size count as one; enums as their integer; `packed T[N]` as `T`), as a byte type (`uint8`, `int8`, `char8`), through a union member, or as part of an object holding it. `*(uint32*)&f` is undefined (the optimizer may move it); use `reinterpret_cast<uint32>(f)` (numbers of one size keep their bits), a union, `memcpy` or a `uint8*`. `-fno-strict-aliasing` turns the rule off for a build.

Primitives: `void`, `bool`, `int8/int16/int32/int64`, `uint8/uint16/uint32/uint64`, normalized `snorm*`/`unorm*` (GPU-style fractions: `snorm8 s = 0.5f;` stores 64, an integer is the stored encoding, arithmetic runs in float), `float` (Float32), `double` (Float64); and char types `char8` (byte), `char16`, `char32`. Built-in aliases: `int` = `int32`, `uint` = `uint32`, `char` = `char8` (the same types, not new ones). `loop` is an ordinary identifier. User object types via `class`, `struct`, `interface`; objects are value types by default.

### Unions
Declare a union inside an object; anonymous unions expose their fields directly on the type:

    struct V { public union { float x, y, z; }; }   // v.x / v.y / v.z all valid
    struct U { union { int32 i; char[4] b; }; }      // named or byte-aliased views

### Packed SIMD vectors & swizzling (SSE)
C4 has **language-level packed vector types**: an N-lane SIMD value in registers, declared with `packed` inside a union - the primary math/vector type for game code.

    class Vec3 { public union { packed float x, y, z; }; }   // 3xf32 = 12 bytes (no pad)
    v.x = 5.0f;                 // single lane
    v.xy = { 1.0f, 2.0f };      // multi-lane write (positional)
    v.zx = src.xy;              // per-lane copy from another run
    v.xyz *= 2.0f;              // elementwise, scalar broadcasts to lanes

- Lane names are one character; the element is any number; at most 16 lanes and 64 bytes. Runs may reorder (`q.wxyz = {9,8,7,6}` writes w=9,x=8,y=7,z=6) and repeat when read (`r.wwww` is a splat), but a write names each lane once. `&v.x` and `&v.xy` are errors.
- `packed T[N]` (2..16 lanes) is the vector as a value: locals, parameters, returns, fields. `v[i]` is a lane; `+ - * / %`, bit ops and unary `-` work lane by lane with a scalar broadcast (`2.0f * v`); comparisons give a `packed bool[N]` mask (combine with `& | ^ !`, test a lane: `if (m[0])`). An array view in the union (`packed float[4] data;`) reads and writes the whole vector: `[operator("create")] void Init(packed float[4] lanes) { this.data = lanes; }`.
- A number broadcasts into a vector wherever it becomes one (`packed float[8] v = 1.0f;`, an argument, a return). `(packed int32[8])v` converts lane by lane; `reinterpret_cast<packed int16[16]>(v)` keeps the bits of a vector of the same size.
- Pointer access works the same (`pp->xy = {10, 20}`). A **pure packed vector** has size exactly `N * sizeof(lane)` (Vec3 = 12), aligned to the largest power of two dividing it (Vec4 16, Vec3 4, `packed float[8]` 32), and passes by value in SIMD registers.
- **The target:** `-march=x86-64-v3` (or `native`, `znver4`, `-mavx2`, `-mno-fma`...) picks the processor for the whole build (and its foreign sources and scripts); `SIMD_AVX2`, `SIMD_FMA`, `SIMD_AVX512F`... are defined for what it has, and `SIMD_FLOAT_LANES` is 4, 8 or 16. Every width up to 64 bytes works on every target.
- **Intrinsics** (`docs/language/14-builtins.md` section 11): `simd_min/max/abs/sqrt/floor/ceil/round/trunc`, `simd_muladd` (fused where possible) and `simd_fma` (needs `fma`), `simd_select(mask, a, b)`, `simd_any/all/bitmask`, `simd_shuffle(a[, b], i...)` with constant indices, `simd_concat`, `simd_splat(x, N)`, `simd_reduce_add/mul/min/max/and/or/xor` (floats in a fixed order), `simd_load(p, N)`, `simd_store`, masked forms, `simd_gather`. Scalars broadcast; `Math::FVector4` and other pure packed objects read as their lanes. Bedrock's `Math::FVector3x8` / `FVector4x8` batch 8 vectors at a time.
- `unpacked float r, g, b, a;` stores lanes as separate struct fields instead of one register - use it for independent field storage / C interop layouts.
- A pure packed vector object fills a run of its lanes whole and combines with one: `v3.zx = v2; v3.xy += v2;`.
- Full swizzle semantics (aliasing views, lowering, rules): `docs/language/11-vectors-and-swizzling.md`.

## Constructors, destructors and operators
Constructors/destructors/operators are ordinary functions tagged with `[operator(...)]`:

    class Type {
        [operator("create")]  public void Init();                 // default constructor (0-arg)
        [operator("copy")]    public void CopyFrom(const Type& o); // copy into a new object
        [operator("delete")]  public void Destroy();              // the destructor (scope exit, delete, GC)
        [operator("+", "add")]public void Add(const Type& other); // either spelling; multiple ops per function when signatures match
    }

Token or canonical name both work (`"+"` == `"add"`, `"+="` == `"add_assign"`); names follow C++ operator names (`add`, `subtract`, `equal`, `notequal`, `less`, ..., `index`, `index_assign`, compound `<op>_assign`). Non-obvious ones: `-` = subtract / negate (unary), `*` = multiply / dereference, `&` = bitwise_and / address_of, `!` = logical_not, `~` = bitwise_not, `++/--` = increment/decrement; `and/or/xor_assign` map to the plain bitwise forms. Full token table: `docs/language/06-lifecycle-and-operators.md`.

Arity is signature-driven: `[operator("-")]` with no parameter is negate and with one is subtract (likewise `*`, `&`, `+`), and every name in a multi-name annotation registers. The operator is a method of the **left** operand's type - operands never swap - so `2.0f * v` needs an extension that `extends float`. `**` is power (built in for numbers; `[operator("power")]` on objects), and `<<=` / `>>=` are `shift_left_assign` / `shift_right_assign`. Lifecycle operators have no token: `"create"`, `"delete"` (the only destructor; `"destroy"` / `"finalizer"` are errors), `"copy"` (takes one `const T&`), `"="` / `"assign"`. There is no `"move"` (an error): objects relocate. **Custom** operators are first-class too (`[operator("hash")]`, `[operator("begin")]`/`[operator("end")]` for iterators, etc.) - libraries call them generically without naming conventions: `v.operator_add(v2);` / `ptr->operator_hash();`.

Lifecycle works like C++ (docs/language/06): construction zeroes the object, builds the class base (its 0-arg create, or the base create a create calls as its FIRST statement: `Base::Init(x);`), default-constructs the members in order, then runs the create body; destruction runs the delete body, then the members in reverse, then the base. Creates are not inherited. Locals, by-value parameters (destroyed by the callee) and temporaries (end of their full expression; a condition's die before the body) are destroyed on every exit, exceptions included; a throwing create destroys the parts it built. Copies use `[operator("copy")]` or go member by member; a type with a `delete` and no copy cannot be copied (error). Temporaries and `return local;` move (no copy); `take(x)` moves out of a variable and leaves it zero. `a = b` uses `[operator("=")]` (`"="` and `"assign"` are one operator, and every name of a multi-name `[operator]` is reachable), else copy + destroy old + move in. Every type must accept zero in its delete. Raw storage: `new (p) T(args)`, `destroy(p)`, `relocate(dst, src)`. Heap arrays: `T* a = new T[n];` / `new T[n]{ e0, e1 }` (run-time `n`; a bad count throws `BadArrayLength`), `delete[] a` (the same as `delete a`). `noexcept` functions stop the program if an exception would leave them.

## Extension methods (`extends`)
Add members to an existing type without modifying it or inheriting from it. `this` is implicit - there is **no** explicit self parameter:

    struct Point { float x; float y; };
    float Length() extends Point;                 // forward decl in the header
    void  SetXY(float nx, float ny) extends Point;
    [operator("add")] Point Add(const Point& o) extends Point;   // operator overloads too

Call them exactly like ordinary members: `p.Length(); p.SetXY(1,2); a = b + c;` (the last dispatches through the `[operator("add")]` extension). **Rule:** operator overloads must be either an extension method (`extends`) or a method inside the struct/class body - a standalone `[operator(...)]` free function with no `extends` is invalid. Extension methods can also act as constructors via `[operator("create")] ... extends T`.

## Function pointers and lambdas
    funcptr<int32(int32)> f;                                             // function pointer type
    funcptr<int32(int32)> g = [](int32 x) -> int32 { return x * 10; };   // explicit `->` return type when returning a value
    h([](int32 v) { printf("%d\n", v); }, 42);                           // no `->` when void
    int32 base = 5;
    func<int32(int32)> add = [base](int32 x) -> int32 { return x + base; };  // func<> holds any callable
    func<int32()> counter = [n = 0]() mutable -> int32 { return ++n; }; // init capture; mutable to write it
- C++ captures: `[x]` copy, `[&x]` reference, `[=]` / `[&]` defaults, `[n = expr]`, `[this]`, `[*this]`. Only locals are captured (globals are used directly); writing a copy capture needs `mutable`.
- A capture-less lambda converts to `funcptr<R(A)>`; any lambda, funcptr or object with `[operator("call")]` (alias `"()"`) converts to `func<R(A)>` (16 bytes: code + environment; a capturing closure's environment is a GC copy). `f == null` / `if (f)` test it.
- Templates can take the closure type directly (`template<F> void Each(F f)`) for zero-cost calls.

## Classes, interfaces and inheritance
- **Single inheritance, multiple interface implementation.** One class base, written first, then any number of interfaces: `class Square : Shape, IArea, INamed { ... }`. An interface is declared like a class (`interface IArea { int32 Area(); }`): no instance fields, methods are public and virtual (pure, or with a default body). Override with `virtual`/`override` (a same-signature method overrides even without the keyword); overloads are separate virtual slots. `Derived*` converts to any base or interface pointer (the address moves to that sub-object; null stays null). Layout, vtables and RTTI follow MSVC, so C++ can call a C4 object through a matching abstract class. Do not list an interface a base already implements (error). `Base::F()` inside a method calls that implementation without dispatch.
- **Checked casts:** `p as Dog*` / `dynamic_cast<Dog*>(p)` (null when the dynamic type is not a Dog; also cross-casts between interfaces), `p is Dog` (bool). They need a polymorphic operand; `as` with a numeric target is an error - use `(T)x`.
- **Visibility** supports both C++ style (`public:` / `private:` / `protected:` tags) and inline per-member modifiers (`public int32 extra;`). The tag does not override the member modifier - it *requires* every declaration in its scope to carry a matching one (a missing one is an error), so visibility is visible without hunting for the tag. Access is checked at every use: `private` is the type's own methods and its nested types, `protected` adds derived types, extension methods are outside code, and interface members are always public.
- **Static members:** `public static int32 Count;`, accessed as `Type.Count`. Static methods likewise.
- **Static storage:** globals and static members start before `main` in declaration order (initializer, braces, or default construction - creates and field initializers run); a static local starts the first time control reaches it, once (thread-safe). All of them are destroyed after `main` returns, last started first. A field may have an initializer: `public int32 count = 3;`.
- **Braced lists:** positional, field designators (`Point p = { y = 2 };`), element designators and ranges (`int32[8] a = { [2...5] = 42 };`, evaluated once and copied). Omitted fields start from their initializers; omitted elements are default-constructed. Naming a field or element twice is an error.
- **Templates:** `template<T>` before a class/struct/function declaration (`class TVector { public union { packed T x,y; }; }`). Function templates can be defined in the `.cf`.

## Everything else you use daily
- Namespaces merge across files (C++ style): `namespace Geo { ... }` then `Geo::Add(1, 2);`.
- Constants: a `const` (or `static const`) whose initializer folds is a compile-time constant, usable in array extents and case labels, in any declaration order; names in it resolve from its own namespace/class (`TAU = PI * 2.0f` inside `namespace Math`). A `const` built by calls or constructors (`static const Mat2 Identity = { Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f) };`) is set once at startup. Write `0.5f`, never `.5f`.
- Control flow is C++ plus extras: `try/catch(T e)/finally` (also `catch (const T& e)`, `catch (T* p)`, catch by base or interface, `catch (...)`; C++ exceptions cross both ways), `defer <stmt>;` (runs at scope exit even on early return), `goto`, `fallthrough;` in switch cases, `is` / `as`. A condition is a bool, number, enum or pointer (a struct is an error). Range-for: `for (int32 v : arr)` copies, `for (int32& v : arr)` writes each element, `for (const T& v : r)` reads without a copy (an iterator's `dereference` must return `T&` for a reference variable). A non-void function must return on every path (`main` returns 0 at its end). A switch selects on an integer, char or enum; each case label must fit the selector's type, appear once, and an enum class takes only its own members.
- Scoped enums: `enum class Traffic { Stop, Slow, Go };` (members referenced qualified); plain `enum` also supported.
- `this` is the receiver: `this.x`, `this->x`, `*this`, and wherever a pointer is wanted (`return this;`, `Node* p = this;`, casts, `?:`) it is the `T*`, as in C++.
- Inline assembly (docs/language/16): `asm(pure, nomem) { "add {sum}, {b}"; inout(reg) sum; in(reg) b; }` - Intel syntax, operands `in` / `out` / `inout` / `const` with register classes `reg`, `xmm`, `ymm`, `zmm` or `"eax"`; file-scope `asm { ... }` adds raw assembly.

## Builtins (always available)
They are declared in the prelude (`Bedrock/Prelude/prelude.ch` in the C4 install), which every file sees with no include; a file-scope declaration of the same name in the program wins. The prelude also declares `_GUID`, and `__uuidof(T)` works for every type (a name-based GUID when `T` has no `[uuid]`).
- C memory/string: `memcpy`, `memset`, `strlen`, `strcmp`, `strncmp`, `memcmp`; variadic `printf` (format string is a `char*`/literal), `fflush`.
- C math: `sqrt/sin/cos/tan/exp/log/pow/floor/ceil/fabs` (double) and the `f`-suffixed float32 forms.
- Size & cast: `sizeof(T)` (`sizeof(expr)` is planned); C-style casts `(int32)x`, `(float)p`; custom casts via `[operator("cast")]`.
- Variadics: user-defined variadic functions read their arguments through `va_list ap; va_start(ap); T x = va_arg(ap, T); va_copy(dst, src); va_end(ap);` (read `double` for a float argument and `int32` for narrower integers).
- Byte helpers: `byte_copy`, `byte_move`, `byte_at`, `set_byte`.
- GC and reflection builtins (`raw_alloc`, `gc_collect`, `reflect_*`, ...) exist too - see the c4-garbage-collector and c4-reflection skills.

## Bedrock (default library)
Bedrock's names live in one-level namespaces: `Core` (containers `Array<T>`, `HashMap<K,V>`, `Queue<T>`, `Stack<T>`, `Sort`; UTF-8 text `StringView`, `String`, `StringBuilder`, `Format`), `FS` (`File`, `Path`, `Directory`; `GetInfo` for kinds, sizes and times, `CopyFile` / `CopyTree`, `AbsolutePath` / `CanonicalPath`, `Watcher` for changes under a directory, `ChunkReader` reading a file ahead on a thread), `Serialization` (DOM, JSON, YAML, binary; `ToJson` / `FromJson` / `ToYaml` / `FromYaml` for any reflected type, strings, arrays, string-keyed maps and enums included; `JsonStream` / `JsonFileStream` / `BinaryStream` decode data while it arrives, value by value), `Reflection`, `GC`, `Threading` (threads, atomics, locks, futures, jobs), `Platform` (virtual memory, the machine, libraries, time) `Net` (addresses, TCP streams and listeners, UDP, `Poll`; blocking calls with millisecond timeouts; `EventLoop` for socket callbacks, timers and work posted from other threads) and `Http` (an HTTP/1.1 `Server` with a thread per connection, `Get` / `Post` / `Fetch`, WebSockets through `AcceptWebSocket` and `WebSocket::Connect`) and `Process` (`Child::Start` with pipes or a terminal, `ReadAny`, `Wait`, `Kill` ending the whole tree; `Run` to capture a program's output). `Core` also has `ToUtf16` / `FromUtf16` (and UTF-16 offsets for LSP positions), `Regex` (RE2 syntax, linear time: `Search`, `Match`, `FindAll`, `Replace`), `Sha1` and `Base64Encode` / `Base64Decode`; `Platform::RandomBytes` gives secure random bytes. Counts and indices are `int64`, element access is unchecked, and a zero container is empty and allocates nothing. Its sources are always compiled into your program, but it is opt-in per file: `include "bedrock.ch"` to bring its declarations into scope. Full reference: docs/library/bedrock.md.

    include "bedrock.ch"
    Core::Array<int32> numbers = { 3, 1, 2 };
    numbers.Sort();
    Core::HashMap<Core::String, int32> ages;
    ages[Core::String("ada")] = 36;
    Core::StringBuilder text;
    Core::Format(&text, "{0:s} is {1}", "ada", ages.Get(Core::String("ada")));

A `String` is never made from a `char*` implicitly (`Core::String s = Core::String("ada");`), but strings and views compare with `==`, `!=`, `<`, `>`, `<=`, `>=`, and with `==` / `!=` against a literal directly: `if (name == "ada")`.

## Conventions
- One concept per file; name files after their primary type or feature. Shared declarations go in `.ch`, definitions in the paired `.cf`.
- Comments stay minimal: self-documenting names first, one short line max when intent is not obvious (see AGENTS.md for the full standard).
