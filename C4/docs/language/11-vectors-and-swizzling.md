# 11 - Vectors and swizzling

C4 has HLSL-style vectors and swizzling as a generic language feature, so a math library (not
the compiler) defines `Vec2`/`Vec3`/`Vec4`/`Color`/... and gets SIMD storage, lane access and
swizzle runs for free. A vector is an ordinary class or struct whose storage is declared as
**lane views** inside a union. Because it is a normal type, it can have constructors, methods,
operators, templates and aliases like any other object.

Goals (from the user's long-term plan): SSE/AVX-backed math that is at least as fast as the
equivalent hand-optimized C++, with left-handed, 0-1 NDC math library conventions on top.

See also: [05 - Objects](05-objects.md) (unions), [06 - Lifecycle and operators](06-lifecycle-and-operators.md),
[compiler/04-codegen-and-abi.md](../compiler/04-codegen-and-abi.md) (lowering),
[library/bedrock.md](../library/bedrock.md) (math library status).

---

## 1. Declaring lane views

**Status:** **Implemented**

```c4
class Vec4 { public union { packed float x, y, z, w; }; }
class Vec3 { public union { packed float x, y, z; }; }
class Int2 { public union { packed int32 a, b; }; }
class RGBA { public union { unpacked float r, g, b, a; }; }
```

A lane view is `packed` or `unpacked`, an element type, and a list of lane names, declared
inside a `union` in a class or struct. The order of the names is the lane index.

| Layout | Storage | LLVM type | Use it for |
|---|---|---|---|
| `packed` | one SIMD register value, lanes contiguous, no padding | `<N x T>` | math vectors |
| `unpacked` | ordinary struct fields at sequential offsets | `{T, T, ...}` | C interop layouts, types that need independent field storage |

Rules:

- Lane names are one character: a run such as `xyz` is read one character per lane, so a longer lane
  name is an error (`a lane name is one character, so 'v.xx' would read lanes: rename 'xx'`). An array
  view (`packed T[2] data`, section 1.1) has a name of any length.
- The element is any number (`float`, `double`, the integer types). A packed view has at most 16 lanes
  and 64 bytes, the widest SIMD register (`'packed double[16]' is 128 bytes: a packed vector holds at
  most 64 (16 floats)`).
- Lane names must be unique within one view.

### 1.1 Aliasing views

Several views in the same union alias the same lanes, which gives HLSL's `xyzw`/`rgba` duality
and an indexable array view:

```c4
template<T> class TVector2 {
	union { packed T x, y; packed T r, g; packed T[2] data; };
}
```

`v.x`, `v.r` and `v.data[0]` are the same lane. Views of the same width and element type share
one register value. The array view is the whole vector as a `packed T[N]` value (section 7): `v.data`
reads it, `v.data = lanes` writes every lane, and `v.data[i]` is one lane.

### 1.2 Pure packed vectors

A type whose only storage is one packed region is a **pure packed vector**:

- `sizeof` is exactly `N * sizeof(T)` with no padding (`Vec3` = 12 bytes, `Vec4` = 16). Its alignment is
  the largest power of two dividing that size (`Vec4` 16, `Vec2` 8, `Vec3` 4, a 32-byte vector 32), so
  an array of vectors has no gaps and every element is aligned. `struct { Vec3 p; float t; }` is 16 bytes.
- It is passed and returned by value as its vector, matching clang (XMM, YMM or ZMM registers on Windows x64 by
  the target, section 8).
- It can be a field of other structs (`struct Mat4 { Vec4 col0, col1, col2, col3; };`), a global
  with a braced initializer, or an array element.

A struct that mixes a packed view with other fields is laid out like any object, with the
vector region where its union is declared, at its aligned offset (`struct { int32 head; union { packed float x, y, z, w; }; }`
is 32 bytes, the vector at 16).

---

## 2. Lane access and swizzle runs

**Status:** **Implemented**

```c4
Vec3 v = { 0.0f, 0.0f, 0.0f };
v.x = 5.0f;                    // single lane write
float a = v.y;                 // single lane read
v.xz = { 1.0f, 1.0f };         // multi-lane write (positional)  -> (1, 0, 1)

Vec3 v3 = { 0.0f, 0.0f, 0.0f };
Vec3 src = { 2.0f, 1.0f, 0.0f };
v3.zx = src.xy;                // per-lane copy: z <- 2, x <- 1   -> (1, 0, 2)

Vec4 q = { 0.0f, 0.0f, 0.0f, 0.0f };
q.wx = { 1.0f, 2.0f };         // reordered run: element i goes to the i-th NAMED lane -> w = 1, x = 2
float s = LaneSum4(q.wwww);    // repeated lanes read as a splat (w, w, w, w); a run binds to a
                               // by-value parameter of a pure packed vector with the same width
```

| Form | Meaning |
|---|---|
| `v.x` | one lane (scalar lvalue) |
| `v.xy`, `v.zyx` | a run: a temporary sub-vector of K lanes (K >= 2) in the given order |
| `v.wwww` | repeated lanes are allowed when reading (splat) |
| `v.xy = { a, b }` | braced write: element i goes to the i-th lane named in the run (HLSL destination order) |
| `v.zx = w.xy` | run-to-run copy, lane by lane |
| `v3.zx = v2` | a pure packed vector object (section 1.2) of the same lanes is that vector: it fills the run whole, and combines with a run in operators (`v3.xy += v2`, `q.xw + v2`) |
| inside a method, bare `x`, `xy`, `data` | resolve to `this` lanes and views |
| `v.zyx[i]`, `v.data[i]` | lane i of the run or vector, read or written; `i` may be a runtime value |
| `p->xy = { ... }` | the same through a pointer |

A run is a value computed for the current expression; it is not a separate object.

### 2.1 Arithmetic on runs

**Status:** **Implemented**

Runs combine elementwise, and a scalar operand broadcasts to every lane:

```c4
v.xyz *= 2.0f;                 // every lane * 2
p.xy += 3.0f;                  // other lanes untouched
w.xyz = w.xyz * 2.0f;
f.xyz *= 3;                    // int scalar converts per lane into a float vector
this.xy += other.xy;           // run op run (inside a method)
```

Whole-vector operators between two vector OBJECTS (`a + b` where both are `Vec3`) are not built
in: the type's author writes them with `[operator("add")]` etc., usually in terms of runs. This
keeps the compiler minimal and lets the math library choose semantics (for example `*` as
component-wise vs dot).

```c4
template<T> class TVector2 {
	union { packed T x, y; packed T r, g; packed T[2] data; };

	[operator("add")] public TVector2<T> Add(const TVector2<T>& o) {
		TVector2<T> res;
		res.x = this.x + o.x;
		res.y = this.y + o.y;
		return res;
	}
}

alias FVec2 = TVector2<float>;
```

Templates of vectors and aliases to them (`alias FVec2 = TVector2<float>;`, a common engine pattern)
work, including through namespaces.

---

## 3. Rules

**Status:** **Implemented** (D-37)

| Rule | Error |
|---|---|
| A lane of a packed vector has no address: lanes are register lanes, not memory. The whole vector is addressable (`&v`). `&v.x` and `&p[i]` of a `packed T[N]` are errors. | `a lane of a packed vector has no address: copy it into a variable first, or take the address of the whole vector` |
| A run has no address and no lifetime: `&v.xy` is an error. | `a swizzle run has no address: copy it into a 'packed' variable first` |
| A write target names each lane once: `v.xx = ...` would store lane x twice. Repeated lanes still read, as a splat (`v.wwww`). | `this swizzle names a lane twice, so it cannot be written to: name each lane once (it can still be read, as a splat)` |
| A lane name is one character (section 1). | `a lane name is one character, so 'v.xx' would read lanes: rename 'xx'` |
| A packed view has 1 to 16 lanes and at most 64 bytes; a `packed T[N]` value has 2 to 16. | `'packed int8[17]' is not a vector: a packed vector has 2 to 16 lanes` |
| A pure packed vector class (its only state is one packed view) has no `[operator("delete")]` or `[operator("copy")]`: it copies and goes away as one register value. | `'V' is a packed vector, which copies and goes away as one register value: it cannot have [operator("delete")] (keep what needs it in a class that holds the vector)` |

Vectors start zeroed like every local, and copy as one register move. A pure packed vector class's default
create runs like any class's (`Quat q;` runs `Init()`), for locals, arrays, fields and globals; an empty one
(`Init() {}`) is never called. Its `[operator("=")]` runs for an assignment (`a = b;`), while initializing
(`Quat b = a;`), passing and returning move the register.

---

## 4. Lowering (implementation guidance)

**Status:** **Implemented** for the basics, **Planned** for the optimization passes.

| Source | Packed lowering | Unpacked lowering |
|---|---|---|
| `v.x` read | `extractelement` | load / `extractvalue` of the field |
| `v.yzx` read | `shufflevector v, poison, <1, 2, 0>` | per-lane extracts assembled into a `<3 x T>` |
| `v.x = s` | `insertelement` | store / `insertvalue` |
| `v.yzx = w` | per-lane `insertelement` | per-field stores |
| `v.xyz *= s` | splat `s`, vector op on the selected lanes | per-lane ops |

Guidance carried forward from the design notes:

- Use `poison` (never `undef`) as the don't-care operand of `shufflevector`.
- **Composition pass (Planned):** fold `swizzle(swizzle(v, P), Q)` into one shuffle, drop identity
  shuffles, cancel inverse pairs. LLVM's InstCombine already composes most shuffles at `-O2`, so
  this is only needed if measurements show missed cases.
- **Unpacked-to-packed promotion (Planned):** when an unpacked view is proven to have no padding
  and no interleaved fields, bitcast it to the packed form to reuse the shuffle path.

---

## 5. Superseded design: the `swizzle` keyword

An earlier note proposed a dedicated top-level declaration:

```text
swizzle Vec4 { packed float x, y, z, w; };
```

This was **superseded** by lane views inside ordinary classes, because a vector needs
constructors, operators, methods, templates and multiple aliasing views, which a special
declaration kind would have had to re-invent. There is no `swizzle` keyword. The note's
semantic rules (section 3) and lowering rules (section 4) still apply.

---

## 6. SIMD beyond lane views

| Item | Status |
|---|---|
| Lane views lower to LLVM vector types; LLVM selects SSE/AVX instructions | **Implemented** |
| The processor a build targets (`-march`, `-m<feature>`) and its `SIMD_*` macros | **Implemented** - section 8 (D-62) |
| SIMD intrinsics (`simd_min`, `simd_shuffle`, `simd_fma`, `simd_gather`, ...) | **Implemented** - section 9, [builtins section 11](14-builtins.md#11-simd-intrinsics) |
| `asm` blocks | **Implemented**: [16 - Inline assembly](16-inline-assembly.md) (`xmm`, `ymm` and `zmm` operands hold packed vectors) |
| Wider lane counts (AVX 8 x float, AVX-512 16 x float) | **Implemented** - alignment, calls and containers tested on every target (section 8) |
| Performance validation of vector math vs equivalent C++ (benchmarks in `Benchmarks/`) | **Implemented** - `Benchmarks/math/` times Bedrock Math against GLM (plain and with its SSE paths) and checks both compute the same results |
| Math library (vec/mat/quat, left-handed, 0-1 NDC, GLM parity) | **Implemented** - `Math::` in `Bedrock/Math/`, opt-in through `include "math.ch"` (D-46); see [library/bedrock.md](../library/bedrock.md#9-math-math-include-mathch) |

---

## 7. Packed arrays as values

**Status:** **Implemented** (D-36)

`packed T[N]` is the SIMD vector `<N x T>` as a value type of its own. It is what a vector's constructor
takes and what its array view reads, so a math library can move whole vectors without naming lanes:

```c4
class Vec4 {
	public union { packed float x, y, z, w; packed float[4] data; };
	[operator("create")] public void Init(packed float[4] lanes) { this.data = lanes; }
	public packed float[4] Lanes() const { return this.data; }
}

packed float[4] Twice(packed float[4] v) { return v * 2.0f; }

packed float[4] a = { 1.0f, 2.0f, 3.0f, 4.0f };
Vec4 v = Vec4(Twice(a));        // (2, 4, 6, 8)
packed bool[4] big = a > 2.5f;  // (false, false, true, true)
float third = a[2];
```

- It can be a local, a global, a parameter, a return, a field, an array element (`packed float[4][2]` is
  two vectors) and a template argument shape (`packed T[N]`). It is passed and returned in SIMD registers
  like a pure packed vector.
- `N` is a constant from 2 to 16, `T` is a number, and the vector holds at most 64 bytes. `sizeof(packed
  float[4])` is 16.
- `v[i]` is one lane, read or written, with a constant or a runtime index. A lane has no address
  (section 3).
- Braces fill the lanes in order; lanes no entry names are zero (`packed float[4] p = { 7.0f };`).
  `v = { ... }` assigns every lane.
- The built-in operators work lane by lane: `+ - * / %` and unary `-` on numbers; `& | ^ ~ << >>` on
  integer lanes (unsigned lanes divide, compare and shift unsigned). A scalar operand converts to the
  element type and broadcasts to every lane, on either side (`2.0f * v`, `v + 1`). Two vectors must have
  the same element type and lane count. Compound assignment (`v += w`, `v *= 0.5f`) works the same way.
- A comparison gives a mask, `packed bool[N]`, one answer per lane. A mask combines with `& | ^` and `!`
  and compares with `==` and `!=`; other operators on a mask are errors. A mask is not a condition: test a
  lane (`if (big[3])`).
- A swizzle run of a class's packed view is a `packed T[K]` value (`packed float[2] xy = v.zw;`), and a
  `packed T[N]` converts to a pure packed vector object of the same lanes.
- A number converts to a `packed T[N]` by broadcasting: it converts to `T` and goes to every lane, wherever a
  value becomes a vector - an initialization (`packed float[8] v = 1.0f;`), an assignment, an argument, a return
  (D-62). The number is evaluated once. A scalar overload of the same function is a better match than the
  broadcast.
- Casts convert lane by lane: `(packed int32[8])v` and `static_cast<packed int32[8]>(v)` convert each lane as the
  scalar cast does (floats truncate toward zero, a `packed bool[N]` lane is 0 or 1, unsigned lanes zero-extend),
  so the lane counts must match. `(packed float[4])2` broadcasts. `reinterpret_cast<packed int16[8]>(v)` keeps the
  bits of a vector of the same size (a mask has no bits to keep). A pure packed vector object casts as its lanes
  (`(packed int32[4])someVec4`). A vector does not cast to one value (read a lane) or to an object (construct it).
- Errors: `packed float f;` says `'packed' needs a lane count: write 'packed float[N]' for a vector value
  (a lane view, 'packed float x, y;', goes inside a union)`; `packed int32*[4]` says the element is a
  number; mixing shapes says `two packed vectors combine only with the same element and lane count`.

---

## 8. The SIMD target

**Status:** **Implemented** (D-62)

One build compiles for one processor, chosen on the command line (and the LSP's `target` option):

| Flag | Selects |
|---|---|
| `-march=<cpu>` | the processor: `x86-64` (the default, SSE2), `x86-64-v2` (SSE4.2), `x86-64-v3` (AVX2, FMA), `x86-64-v4` (AVX-512), any LLVM x86-64 CPU (`haswell`, `znver4`, ...) or `native`, the building machine |
| `-m<feature>`, `-mno-<feature>` | one feature on or off: `sse3 ssse3 sse4.1 sse4.2 popcnt avx avx2 fma f16c bmi bmi2 lzcnt avx512f avx512vl avx512bw avx512dq avx512cd` |

Flags read as clang reads them: the last `-march` names the processor, then each `-m` flag applies in order,
with what it implies (`-mavx2` turns on `avx`; `-mno-avx` turns off `avx2` and `fma`). Every function of the
program is compiled for the target, foreign C/C++ sources are compiled and parsed with the same flags, and a
script host records it in its `.c4i`, so its scripts compile and run for exactly that processor
([scripting](../runtime/04-scripting.md)). All code of one program agrees on how a vector is passed.

Each enabled feature defines a macro (= 1): `SIMD_SSE3`, `SIMD_SSSE3`, `SIMD_SSE4_1`, `SIMD_SSE4_2`, `SIMD_AVX`,
`SIMD_AVX2`, `SIMD_FMA`, `SIMD_AVX512F`, `SIMD_AVX512VL`, `SIMD_AVX512BW`, `SIMD_AVX512DQ`. `SIMD_FLOAT_LANES` is
4, 8 or 16: the floats the widest register holds, a width for code that adapts:

```c4
alias Wide = packed float[SIMD_FLOAT_LANES];   // 16 lanes with AVX-512, 8 with AVX, 4 otherwise
#if SIMD_FMA
	r = simd_fma(a, b, c);                     // one rounding
#else
	r = simd_muladd(a, b, c);
#endif
```

Widths, alignment and passing:

- Every width (2 to 16 lanes, at most 64 bytes) works on every target. A vector wider than the target's registers
  is split: a `packed float[8]` is two XMM registers without AVX, one YMM register with it.
- A vector's alignment is the largest power of two dividing its size (32 for `packed float[8]`, 64 for
  `packed float[16]`), on the stack, in globals and fields, on the managed heap and in Bedrock's containers
  (which allocate with `raw_alloc_aligned`, [builtins 4](14-builtins.md#4-memory-and-gc)). Code built for AVX
  reads them with aligned instructions.
- Win64 passes a vector argument by hidden reference and returns a vector in XMM0, YMM0 or ZMM0 when the target
  has that register (in XMM0 and XMM1 otherwise), as clang does for `__m128` / `__m256` / `__m512`.

---

## 9. SIMD intrinsics

**Status:** **Implemented** (D-62) - the full table is [builtins section 11](14-builtins.md#11-simd-intrinsics)

The `simd_*` intrinsics take `packed T[N]` values (a pure packed vector object such as `Math::FVector4` reads as
its lanes) and give `packed` values or scalars. They cover what the operators do not: lane math (`simd_min`,
`simd_max`, `simd_abs`, `simd_sqrt`, rounding, `simd_muladd`, `simd_fma`), masks (`simd_select`, `simd_any`,
`simd_all`, `simd_bitmask`), lane rearranging (`simd_shuffle` with constant indices, `simd_concat`, `simd_splat`),
horizontal reductions (`simd_reduce_add` and the others) and memory (`simd_load`, `simd_store`, masked forms,
gathers).

```c4
packed float[8] clamped = simd_min(simd_max(v, 0.0f), 1.0f);           // scalars broadcast
packed float[8] relu = simd_select(v > 0.0f, v, 0.0f);
packed float[4] reversed = simd_shuffle(q, 3, 2, 1, 0);
packed float[4] low = simd_shuffle(a, b, 0, 4, 1, 5);                  // interleave two vectors
float total = simd_reduce_add(lanes);                                  // the same at -O0 and -O2
packed float[8] gathered = simd_gather(table, indices);                // table[indices[i]]
uint32 hits = simd_bitmask(distances < radius);
```

A float reduction combines in a fixed order (the upper half of the lanes into the lower half), so its result
does not depend on the optimizer or the target. `simd_fma` needs `fma` in the target; `simd_muladd` is the
portable form, fused where the target can.
