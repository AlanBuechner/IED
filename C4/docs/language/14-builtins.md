# 14 - Builtins

Builtins are what every program sees with no include: the small set of C runtime functions C4 code
needs before any library exists, the hooks into the C4 runtime (GC, reflection, file I/O, tests), the
`_GUID` type, `BadCast`, what a checked cast to a reference throws, and `BadArrayLength`, what `new T[n]` throws
for a count that cannot hold its elements ([10](10-expressions.md)). They are ordinary C4 declarations in the **prelude**, `Bedrock/Prelude/prelude.ch`, which the
compiler makes every unit include (D-43). Only the intrinsics of section 1, the `va_*` forms of section 8, the
atomics of section 10, the SIMD intrinsics of section 11 and the bit intrinsics of section 12 stay in the compiler.

Rules:

- The prelude declares each function with `[link("symbol")]`: C runtime functions keep their names, C4 runtime
  ones are prefixed `c4_` (`raw_alloc` calls `c4_raw_alloc`).
- A program declaration of the same file-scope name, a function or a type, hides the prelude's: the prelude is a
  fallback, never an override. A name inside a namespace hides nothing. A hidden function stays the one a file
  calls where the program's declaration is not visible: Bedrock keeps the prelude's `memmove` beside a bound C
  header that declares the C runtime's (the prelude's C functions have the C runtime's signatures, so both name one
  function).
- Prelude parameters are primitives, `void*` or `const void*`. Any pointer converts to them; string literals are `char8*`.
- Prelude functions are the runtime's, not the program's: reflection does not list them. Go-to-definition in the
  editor opens their declaration in the prelude.
- A compile without the default library (`Bedrock/` missing) has no prelude; `C4.exe` reports a missing
  `Bedrock/` as an error.
- Normal code should prefer [Bedrock](../library/bedrock.md) wrappers (`Core::String`, `FS::File`,
  `Reflection::Type`, containers) over raw builtins.

---

## 1. Compiler intrinsics

**Status:** **Implemented**

| Intrinsic | Meaning |
|---|---|
| `sizeof(T)` / `sizeof(expr)` | size in bytes, `int64` constant; the expression is not evaluated ([10](10-expressions.md#41-sizeof-alignof-offsetof)) |
| `__uuidof(T)` | pointer to the type's 16-byte `_GUID`: its `[uuid("...")]` ([13](13-interop.md)), else the RFC 4122 version 5 UUID of `"c4:"` + its qualified name in the URL namespace (`uuid.uuid5(uuid.NAMESPACE_URL, "c4:Ns::Box<int32>")` in Python), so every type has one (D-43) |
| `nameof(T)` / `nameof(expr)` | the type's qualified name as reflection registers it (D-50): `int32`, `Geo::Point*`, `Core::Array<Geo::Point>`, a string literal; the expression is not evaluated. Generic code finds a template argument's reflection with it: `Reflection::Type(nullptr).FindByName(nameof(T))` (D-72) |
| `__init_list<T>` | built-in initializer-list type: a braced list passed to it becomes `{data, size}`; maps to C++ `std::initializer_list<T>` |
| `take(x)` | the value of the variable, field or element `x`, moved out; `x` is left zero ([06](06-lifecycle-and-operators.md#64-copy-move-and-assignment)) |
| `new (p) T(args)` | builds a `T` in the storage `p` points to (any pointer): zeroes it, then constructs as `T(args)` does (a create, an aggregate fill, or a copy for `new (p) T(x)` with `x` a `T`); yields `p` as a `T*`. Nothing is registered with the GC: the caller destroys it |
| `destroy(p)` | runs the destruction of the `T` at `p` (a `T*`, not `void*`); the storage is then raw memory. Does nothing for a type with nothing to destroy. One object: destroy an array element by element |
| `relocate(dst, src)` | moves the `T` at `src` into the raw storage at `dst` (both `T*`, neither pointing to const) by copying its bytes, and leaves `*src` zero. Nothing is constructed or destroyed |
| `native_bytes_of(x)` / `native_bytes_of(p, count)` | the native memory (outside the GC heap) the variable, field, element or `*p` `x` holds, or the `count` elements a `T*` `p` points to hold: an `int64`. A type's `[operator("native_bytes")] int64 NativeBytes() const` answers for its whole object; a type without one adds up its bases, its fields (not static, not in an anonymous union) and its fixed arrays' elements; pointers, references and unions hold none, since what a pointer points at is counted on its own. A type that can hold none gives 0. The operands are evaluated once either way. Bedrock's containers count their buffers by capacity plus what their elements hold. The GC census sums it per type ([runtime/01](../runtime/01-memory-and-gc.md)) |
| `holds_native_bytes(T)` | whether a value of `T` can hold native memory (`native_bytes_of` is not always 0 for it): a `bool` constant, as `sizeof` is an `int64` one. A container skips walking elements that hold none: `if (holds_native_bytes(E)) ...` |

The last three are how a container manages objects in a raw buffer (Bedrock's `Array`, `Queue`, `Stack`
and `HashMap` do): construct into a free slot, destroy a removed element, relocate elements when the
buffer moves. Copying the bytes of a whole range of live objects is also a valid relocation, because
objects are trivially relocatable. A program function named `take`, `destroy`, `relocate` or `native_bytes_of` wins
over the intrinsic.

The census calls a type's `native_bytes` operator while every other thread is stopped wherever it was, so the
operator only reads: no allocation, no locks, nothing it would not do in the middle of another thread's change to the
object (a read that faults counts 0). An operator that is static, takes arguments or returns a non-integer is an error
where `native_bytes_of` or `new` of the type needs it.

## 2. C memory and strings

**Status:** **Implemented**

| Builtin | Signature |
|---|---|
| `memcpy` | `void* memcpy(void* dst, const void* src, uint64 n)` - returns `dst`, as C's does |
| `memmove` | `void* memmove(void* dst, const void* src, uint64 n)` - the ranges may overlap; returns `dst` |
| `memset` | `void* memset(void* dst, int32 value, uint64 n)` |
| `memcmp` | `int32 memcmp(void* a, void* b, uint64 n)` |
| `strlen` | `uint64 strlen(void* s)` |
| `strcmp` | `int32 strcmp(void* a, void* b)` |
| `strncmp` | `int32 strncmp(void* a, void* b, uint64 n)` |
| `printf` | `int32 printf(void* fmt, ...)` - variadic; floats promote to double |
| `fflush` | `int32 fflush(void* stream)` - pass `null` to flush all |

## 3. C math

**Status:** **Implemented**

| float (`float`) | double (`double`) |
|---|---|
| `sqrtf sinf cosf tanf asinf acosf atanf sinhf coshf tanhf expf logf log2f log10f floorf ceilf roundf truncf fabsf` (1 arg) | `sqrt sin cos tan asin acos atan sinh cosh tanh exp log log2 log10 floor ceil round trunc fabs` (1 arg) |
| `powf atan2f fmodf` (2 args) | `pow atan2 fmod` (2 args) |

A full math library (vectors, matrices, quaternions) is library work, not builtins: Bedrock's `Math`
namespace, `include "math.ch"` ([Bedrock](../library/bedrock.md)). `round` rounds halves away from zero; `fmod`
keeps the sign of its first argument.

## 4. Memory and GC

**Status:** **Implemented** - details in [runtime/01-memory-and-gc.md](../runtime/01-memory-and-gc.md)

| Builtin | Signature | Notes |
|---|---|---|
| `raw_alloc` | `void* raw_alloc(uint64 size)` | unmanaged memory, never scanned or collected |
| `raw_free` | `void raw_free(void* p)` | |
| `raw_free_sized` | `void raw_free_sized(void* p, uint64 bytes)` | `raw_free` for a block of `bytes`, the size asked of `raw_alloc`: the runtime's native-memory count needs no heap lookup (what Bedrock's containers use) |
| `raw_alloc_aligned` | `void* raw_alloc_aligned(uint64 size, uint64 align)` | unmanaged memory aligned to `align`, a power of two (`raw_alloc` gives 16): for elements such as a `packed float[8]` |
| `raw_free_aligned` | `void raw_free_aligned(void* p)` | frees what `raw_alloc_aligned` gave, and only that |
| `buffer_resize` | `void* buffer_resize(void* buffer, uint64 oldBytes, uint64 keepBytes, uint64 newBytes)` | a buffer that grows: heap memory below 1 MiB; from 1 MiB up, reserved address space whose pages are committed as it grows, so it grows in place (the reservation holds 8 times the size, at least 64 MiB) and moves only when it outgrows that. `oldBytes` is the size asked for last (0 with a null `buffer`): it alone says which kind the buffer is. Keeps the first `keepBytes`; null for `newBytes` 0, and null with `buffer` untouched when memory runs out. StringBuilder's text and Array's elements live in these |
| `buffer_free` | `void buffer_free(void* buffer, uint64 bytes)` | frees a `buffer_resize` buffer of `bytes` (the size asked for last) |
| `buffer_resize_scanned`, `buffer_free_scanned` | as `buffer_resize` and `buffer_free` | for a buffer the collector scans for pointers: it is a scan region at its whole size, the region never covers memory given back, and the contents stay in a registered region while they move |
| `gc_collect` | `void gc_collect()` | force a collection (works even while auto-collection is disabled) |
| `gc_register_region` | `void gc_register_region(void* start, uint64 size)` | conservatively scan this raw memory for GC pointers (how containers keep `T*` elements alive) |
| `gc_unregister_region` | `void gc_unregister_region(void* start)` | call before freeing the region |
| `gc_allocs` | `uint64 gc_allocs()` | total managed allocations |
| `gc_collections` | `uint64 gc_collections()` | collections run |
| `gc_heap_bytes` | `uint64 gc_heap_bytes()` | memory the heap holds from the OS |
| `gc_free_bytes` | `uint64 gc_free_bytes()` | heap memory not holding objects |

The rest of the collector API - pacing, disable / enable, pins, root enumerators, collection callbacks and
the full statistics - is the Bedrock `GC` namespace (`include "Memory/gc.ch"`,
[Bedrock](../library/bedrock.md#7-memory-memorygcch)).

## 5. Bytes and number text

### 5.1 Byte helpers

**Status:** **Implemented**

| Builtin | Signature |
|---|---|
| `byte_copy` | `void byte_copy(void* dst, void* src, int64 len, int64 dstOff, int64 srcOff)` |
| `byte_move` | `void byte_move(void* dst, void* src, int64 len, int64 dstOff, int64 srcOff)` - overlap-safe |
| `byte_at` | `int32 byte_at(void* p, int64 offset)` |
| `set_byte` | `void set_byte(void* p, int64 offset, int32 value)` |

### 5.2 Number text

**Status:** **Implemented** - Bedrock's `Core::StringBuilder` uses these; nothing comes from printf. The shortest form's digits are Schubfach's (`C4Runtime/ShortestDecimal.h`, the same digits as Ryu's) laid out sixteen at a time in SSE2 lanes with no branch on the digits (`C4Runtime/ShortestDouble.cpp`), 1.5x faster than `to_chars`; fixed decimals come from the C++ library's `<charconv>` (correctly rounded); ordinary decimals parse on the Eisel-Lemire fast path (`C4Runtime/DecimalToDouble.h`), 3.5x faster than `from_chars`

| Builtin | Signature | Notes |
|---|---|---|
| `format_double` | `int32 format_double(char* out, int32 cap, double value, int32 decimals)` | `decimals` fraction digits, or with a negative `decimals` the shortest text that reads back as `value` (plain decimals for exponents -5 to 15, scientific otherwise); NaN and the infinities are `nan`, `inf`, `-inf` on every platform. The length written (null-terminated), -1 when `out` is too small |
| `parse_double` | `int64 parse_double(char* text, int64 length, double* out)` | the double that `text[0, length)` starts with (C `strtod` syntax); the bytes it took, 0 when there is no number. Read in place; only leading space, a `+` or a hexadecimal float (`0x1p3`) takes strtod's slower path, which copies the bytes |

## 6. File I/O

**Status:** **Implemented** (basic) - Bedrock's `FS::File` class wraps these with RAII. Paths are UTF-8 (on Windows
the runtime converts them for the wide CRT calls).

| Builtin | Signature | Notes |
|---|---|---|
| `file_open` | `void* file_open(char* path, char* mode)` | stdio modes (`"rb"`, `"wb"`, ...); returns `null` on failure; the handle is not GC managed |
| `file_read` | `int64 file_read(void* h, void* buf, int64 len)` | bytes read, -1 on error |
| `file_write` | `int64 file_write(void* h, void* buf, int64 len)` | bytes written, -1 on error |
| `file_flush` | `int32 file_flush(void* h)` | 0 / -1 |
| `file_close` | `int32 file_close(void* h)` | 0 / -1 |
| `file_seek` | `int64 file_seek(void* h, int64 offset)` | from the start; the new position, -1 on error |
| `file_seek_from` | `int64 file_seek_from(void* h, int64 offset, int32 origin)` | origin 0 start, 1 current, 2 end; the new position, -1 on error |
| `file_tell` | `int64 file_tell(void* h)` | |
| `file_size` | `int64 file_size(char* path)` | -1 when there is no such regular file |
| `file_exists` | `int32 file_exists(char* path)` | 1 when the path is an existing regular file (not a directory) |
| `file_delete` | `int32 file_delete(char* path)` | |

Positions are 64-bit.

## 6.1 Directories

**Status:** **Implemented** - Bedrock's `FS::Directory`, `FS::Path` and the `FS` tree functions wrap these

| Builtin | Signature | Notes |
|---|---|---|
| `fs_is_directory` | `int32 fs_is_directory(char* path)` | 1 for a directory (a link to one counts) |
| `fs_is_link` | `int32 fs_is_link(char* path)` | 1 for a symbolic link or junction |
| `fs_make_directory` | `int32 fs_make_directory(char* path)` | 0 / -1; one level |
| `fs_remove_directory` | `int32 fs_remove_directory(char* path)` | 0 / -1; an empty directory or a directory link |
| `fs_rename` | `int32 fs_rename(char* from, char* to)` | 0 / -1; replaces a file at `to` |
| `fs_current_directory` | `int64 fs_current_directory(char* out, int64 cap)` | the UTF-8 length; written (null-terminated) only when it fits |
| `fs_temp_directory` | `int64 fs_temp_directory(char* out, int64 cap)` | the same, for the temporary-files directory |
| `fs_directory_open` | `void* fs_directory_open(char* path)` | a listing handle, null when `path` is not a readable directory |
| `fs_directory_next` | `int32 fs_directory_next(void* h, char* name, int32 cap, int32* kind)` | the next entry (not `.` / `..`): its name length, 0 at the end, -1 when `name` is too small (the entry stays); `kind` 0 file, 1 directory, 2 link |
| `fs_directory_close` | `void fs_directory_close(void* h)` | |

## 7. Reflection

**Status:** **Implemented** - use the Bedrock `Reflection` namespace instead; full description in
[runtime/02-reflection.md](../runtime/02-reflection.md)

About 85 low-level builtins operate on opaque handles (`void*`). Name accessors return
`const char*`. Groups:

| Group | Builtins |
|---|---|
| registry lookup | `reflect_type`, `reflect_type_by_index`, `reflect_count`, `reflect_variable`, `reflect_variable_by_index`, `reflect_variable_count`, `reflect_function`, `reflect_function_by_index`, `reflect_function_count`, `reflect_function_containing`, `reflect_kind_of` |
| type queries | `reflect_name`, `reflect_size`, `reflect_align`, `reflect_base_name`, `reflect_is_virtual_type`, `reflect_field_count`, `reflect_method_count`, `reflect_enum_count`, `reflect_type_static_variable_count`, `reflect_type_static_variable_at` |
| fields | `reflect_field`, `reflect_field_at`, `reflect_field_handle_name`, `reflect_field_handle_offset`, `reflect_field_handle_size`, `reflect_field_handle_type_name`, `reflect_field_handle_is_const`, `reflect_field_type_handle` |
| methods | `reflect_method`, `reflect_method_at`, `reflect_method_handle_name`, `reflect_method_handle_param_count`, `reflect_method_handle_is_virtual`, `reflect_method_handle_is_static`, `reflect_method_handle_is_const`, `reflect_method_handle_return_type_name`, `reflect_method_param`, `reflect_method_return_handle` |
| parameters | `reflect_param_handle_name`, `reflect_param_handle_type_name`, `reflect_param_type_handle` |
| enums | `reflect_enum_at`, `reflect_enum_handle_name`, `reflect_enum_handle_value` |
| variables | `reflect_variable_handle_name`, `reflect_variable_handle_type_name`, `reflect_variable_handle_is_const`, `reflect_variable_handle_owner`, `reflect_variable_type_handle`, `reflect_variable_address` |
| functions | `reflect_function_handle_name`, `reflect_function_handle_symbol`, `reflect_function_handle_address`, `reflect_function_handle_param_count`, `reflect_function_handle_is_static`, `reflect_function_handle_owner`, `reflect_function_handle_return_type_name`, `reflect_function_param`, `reflect_function_return_handle` |
| invoke | `reflect_function_is_invokable`, `reflect_method_is_invokable`, `reflect_function_invoke(f, object, result, arguments)`, `reflect_method_invoke(m, object, result, arguments)` |
| annotations | `reflect_<entity>_annotation_count`, `reflect_<entity>_annotation_at` for `type`, `field`, `method`, `param`, `variable`, `function`; `reflect_annotation_name`, `reflect_annotation_arg_count`, `reflect_annotation_arg_kind` (1 int, 2 float, 3 bool, 4 string), `reflect_annotation_arg_int`, `reflect_annotation_arg_float`, `reflect_annotation_arg_string` |
| groups | `reflect_group`, `reflect_group_handle_count`, `reflect_group_handle_item`, `reflect_group_name_count`, `reflect_group_name_at` |
| hot reload | `c4_reflect_unload_source(char* tag)`, `c4_reflect_on_unload(tag, fn, user)`, `c4_reflect_remove_listener(id)` |

## 8. Variadic argument access

**Status:** **Implemented**

```c4
int32 SumInts(int32 count, ...) {
	va_list ap;
	va_start(ap);
	int32 sum = 0;
	for (int32 i = 0; i < count; i++) {
		sum += va_arg(ap, int32);
	}
	va_end(ap);
	return sum;
}
```

| Intrinsic | Does |
|---|---|
| `va_list` | the built-in list type (an alias of `char8*` on Win64: a cursor into the 8-byte argument slots) |
| `va_start(ap)` | opens the arguments of the variadic function it is in |
| `va_arg(ap, T)` | the next argument as a `T`; the second operand is a type, as in `sizeof` |
| `va_copy(dst, src)` | `dst` reads the same remaining arguments as `src` |
| `va_end(ap)` | closes the list |

- On Win64 every argument takes one 8-byte slot; an object of 1, 2, 4 or 8 bytes rides in its slot and any
  other object arrives as a pointer to a copy, as C does it, so `va_arg(ap, Triple)` works for both. Only plain
  data travels this way: an object that copies or destroys itself cannot be passed or read
  ([07-functions-and-lambdas](07-functions-and-lambdas.md#77-c-variadic-functions)).
- `va_arg` reads the promoted type: a `float` arrives as a `double`, and integers narrower than 32 bits as
  an `int32` (reading the narrow type is an error that names the right one).
- A function of the program named `va_start`, `va_end` or `va_copy` wins over the intrinsic; `va_arg` is
  always the intrinsic. The old `va_arg_int` / `va_arg_uint64` / `va_arg_double` / `va_arg_float`
  builtins are gone.

## 9. Test assertions

**Status:** **Implemented** - see [runtime/03-testing.md](../runtime/03-testing.md)

| Builtin | Signature |
|---|---|
| `assert_true` | `int32 assert_true(int32 cond, char* msg)` |
| `assert_eq` | `int32 assert_eq(int64 expected, int64 actual, char* msg)` |
| `assert_feq` | `int32 assert_feq(double expected, double actual, double tolerance, char* msg)` |
| `assert_se` | `int32 assert_se(char* expected, char* actual, char* msg)` - string compare |

Each returns 1 on pass and 0 on failure and records the result for the test summary. The test
runtime is currently linked into every executable, so these calls also link in non-test builds.
Whether assertions should be restricted to `-test` builds (or compiled out in shipping builds) is
an **Open question**.

## 10. Atomics

**Status:** **Implemented** (D-60)

```c4
int64 counter = 0;
atomic_fetch_add(&counter, 1, MemoryOrder::Relaxed);
int64 expected = 1;
if (atomic_compare_exchange(&counter, &expected, 2, MemoryOrder::AcqRel, MemoryOrder::Acquire)) { }
atomic_store(&ready, true, MemoryOrder::Release);
```

| Intrinsic | Does |
|---|---|
| `atomic_load(p, order)` | reads `*p` |
| `atomic_store(p, value, order)` | writes `*p` |
| `atomic_exchange(p, value, order)` | writes `*p`, returning what it held |
| `atomic_compare_exchange(p, expected, desired, success, failure)` | when `*p` holds `*expected`, writes `desired` and returns true; otherwise writes what `*p` held to `*expected` and returns false |
| `atomic_compare_exchange_weak(...)` | the same, but may fail spuriously: for retry loops |
| `atomic_fetch_add` / `_sub` / `_and` / `_or` / `_xor(p, value, order)` | combines `*p` with `value`, returning what it held |
| `atomic_fence(order)` | orders the memory accesses around it |

- `p` points to a number, bool, char, enum or pointer of at most 8 bytes (`fetch_*` only to an integer), and
  every access through it is one indivisible operation. A pointer to const can only be loaded from.
- `order` is a value of the prelude's `enum class MemoryOrder { Relaxed, Acquire, Release, AcqRel, SeqCst }`,
  with C++'s meanings. A load cannot release, a store cannot acquire, a failed compare-exchange only loads
  (its order cannot be `Release` or `AcqRel`), and a fence cannot be `Relaxed`; for a constant order each is an
  error that says why. An order known only at run time (a parameter, as `Atomic<T>` passes its caller's)
  selects among the orders the operation can use, and any other value acts as `SeqCst`; once a call is inlined
  with a constant, the choice folds away.
- A float compare-exchange compares the bits.
- A program function of the same name wins over the intrinsic. Bedrock wraps these in `Atomic<T>`.

## 11. SIMD intrinsics

**Status:** **Implemented** (D-62) - the target and widths are [vectors section 8](11-vectors-and-swizzling.md#8-the-simd-target)

```c4
packed float[8] p = simd_load(&positions[i], 8);
packed float[8] v = simd_muladd(velocity, dt, p);
packed bool[8] outside = simd_abs(v) > 10.0f;
v = simd_select(outside, simd_min(simd_max(v, -10.0f), 10.0f), v);
simd_store(&positions[i], v);
if (simd_any(outside)) bounced += simd_bitmask(outside);
```

The arguments are `packed T[N]` values; a pure packed vector object (its only state one packed view, such as
`Math::FVector4`) reads as its lanes, and a result converts back to one. In the lane-wise and select forms a
scalar argument broadcasts to the vectors' shape. "Number lanes" are integers and floats (not bools, enums or
normalized integers).

| Intrinsic | Gives | Lanes |
|---|---|---|
| `simd_min(a, b)`, `simd_max(a, b)` | per lane `a < b ? a : b` / `a > b ? a : b`: the SSE rule, so a NaN in either lane gives `b` | numbers (unsigned compare unsigned) |
| `simd_abs(a)` | per lane absolute value; a float's sign bit cleared (-0 is 0); the most negative integer stays itself; an unsigned lane is itself | numbers |
| `simd_sqrt(a)`, `simd_floor(a)`, `simd_ceil(a)`, `simd_trunc(a)` | per lane square root and roundings | floats |
| `simd_round(a)` | per lane nearest integer, halves away from zero (as `round`) | floats |
| `simd_muladd(a, b, c)` | `a * b + c`, one fused multiply-add where the target has FMA | floats |
| `simd_fma(a, b, c)` | `a * b + c` with one rounding; needs `fma` in the target (`-mfma`, `-march=x86-64-v3`) | floats |
| `simd_clmul(a, b, i, j)` | in each 128-bit block, the 128-bit carry-less product of `a`'s 64-bit lane `i` and `b`'s lane `j` (`i`, `j` constant 0 or 1: the block's low or high half), low half in the block's first lane; needs `pclmul` (`-mpclmul`) for `packed uint64[2]`, `vpclmulqdq` with `avx` for `[4]` or with `avx512f` for `[8]`. CRC folding and GHASH are built on it | `uint64` or `int64`, 2, 4 or 8 lanes |
| `simd_select(mask, a, b)` | per lane `mask ? a : b`; two scalars give a vector of the mask's lane count | any |
| `simd_any(mask)`, `simd_all(mask)` | a `bool`: some lane / every lane is true | masks |
| `simd_bitmask(mask)` | a `uint32` with bit i set when lane i is true | masks |
| `simd_shuffle(a, i0, i1, ...)` | `packed T[K]` with lane j = `a[ij]`; K (2 to 16) is the index count; indices are constant integers (`N - 1` folds) | any |
| `simd_shuffle(a, b, i0, ...)` | the same over the lanes of `a` then `b`: 0 to N-1 are `a`'s, N to 2N-1 are `b`'s | any |
| `simd_concat(a, b)` | `packed T[2N]`: `a`'s lanes then `b`'s | any |
| `simd_splat(x, N)` | `packed T[N]` with every lane `x`; N a constant | numbers, bools |
| `simd_reduce_add(a)`, `simd_reduce_mul(a)` | the lanes' sum / product | numbers |
| `simd_reduce_min(a)`, `simd_reduce_max(a)` | the smallest / largest lane | numbers |
| `simd_reduce_and(a)`, `simd_reduce_or(a)`, `simd_reduce_xor(a)` | the lanes' bitwise combination | integers |
| `simd_load(p, N)` | `packed T[N]` read from `p` (a `T*` or `const T*`, or an array) at `T`'s alignment | numbers |
| `simd_store(p, v)` | writes `v`'s lanes to `p` (a `T*` to mutable memory) | numbers |
| `simd_load_masked(p, mask)` | the lanes the mask selects read from `p`, the others zero and never read | numbers |
| `simd_store_masked(p, v, mask)` | writes only the lanes the mask selects | numbers |
| `simd_gather(p, indices)` | lane i = `p[indices[i]]`; the indices are integer lanes (signed ones reach before `p`) | numbers |
| `simd_gather_masked(p, indices, mask)` | the same, lanes off zero and never read | numbers |

Rules:

- Float sums, products, minima and maxima of `simd_reduce_*` combine in a fixed order: the upper half of the lanes
  into the lower half until one lane is left, a lane count that is not a power of two padded with the identity
  (`-0` for a sum, `1` for a product, lane 0 for min and max). The result is the same at every optimization level
  and on every target. Integer reductions wrap in the lane type.
- The vectors of one call have one shape (element and lane count); a mask has one lane per lane.
- A lane count, a shuffle index or a splat count is a constant integer (a literal, a `const`, a template value
  parameter, or `+ - *` of those).
- Errors name the call and the shape at fault: `simd_min's vectors are a 'packed float[8]' and a 'packed float[4]'`,
  `simd_shuffle's lane index 4 is out of range: the vectors have lanes 0 to 3`, `simd_sqrt works on float or
  double lanes, not a 'packed int32[4]'`, `simd_fma needs the processor's fused multiply-add: build with -mfma or
  -march=x86-64-v3 (or newer), annotate the function [target("fma")], or use simd_muladd`, `simd_clmul's first lane is
  2: it picks the low (0) or high (1) 64 bits of each 128-bit block`.
- A program function of the same name wins over the intrinsic.

## 12. Bit intrinsics

**Status:** **Implemented**

Integer bit operations the processor does in one instruction (or a few), on any integer type, signed or unsigned, of
8 to 64 bits, and on packed integer vectors lane by lane. Hashing, bit sets and hash tables are built on them.

```c4
uint64 h = a * b ^ mul_high(a, b);          // the 128-bit product folded, as fast hashes do
int32 first = bit_trailing_zeros(mask);      // the lowest set bit of a mask
uint32 r = bit_rotate_left(x, 13);
```

| Intrinsic | Result |
|---|---|
| `bit_count(x)` | how many bits of `x` are set |
| `bit_leading_zeros(x)` | how many zero bits come before the highest set bit; the width for 0 |
| `bit_trailing_zeros(x)` | how many zero bits come after the lowest set bit; the width for 0 |
| `bit_rotate_left(x, n)`, `bit_rotate_right(x, n)` | `x`'s bits rotated by `n` modulo the width (so `-1` rotates by width - 1); `n` converts to `x`'s type |
| `byte_swap(x)` | `x` with its bytes in reverse order (an 8-bit value is itself) |
| `mul_high(a, b)` | the upper half of the double-width product of `a` and `b`, signed or unsigned as their type is; `b` converts to `a`'s type. `a * b` is the lower half |

Rules:

- A count is an `int32` for an integer and the vector's own type for a vector; the other intrinsics give `x`'s (or
  `a`'s) type.
- A vector operand takes a vector of the same lanes or a scalar, which applies to every lane.
- Errors name the call and what is wrong: `bit_count works on an integer or a packed integer vector, not a 'float'`,
  `bit_rotate_left takes 2 arguments, not 1`.
- A program function of the same name wins over the intrinsic.

## 13. Optimizer hints

**Status:** **Implemented**

Hints that change how fast a program runs, never what it computes: which way a branch usually goes, and memory the
program is about to read.

```c4
if (unlikely(count > capacity)) Grow(count);    // the rare path laid out of the way
for (int64 i = 0; i < n; ++i) {
	mem_prefetch(&table[keys[i + 8]]);          // the read eight steps ahead, started now
	sum += table[keys[i]];
}
```

| Intrinsic | Meaning |
|---|---|
| `likely(c)`, `unlikely(c)` | `c` as a `bool` (it converts as a condition does), with the way it usually goes: the optimizer lays out and predicts the branch it decides (`llvm.expect`) |
| `mem_prefetch(p)` | starts reading the cache line `p` points to into every cache level (`llvm.prefetch`, a read of data); any pointer, valid or not, since nothing is read through it. Returns nothing |

A program function of the same name wins over the intrinsic.
