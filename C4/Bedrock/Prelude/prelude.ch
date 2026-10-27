// The prelude: what every C4 program sees without an include. These are ordinary declarations bound to
// the C runtime and the C4 runtime with [link]; a program that declares the same name at file scope uses
// its own instead (docs/language/14-builtins.md).

// --- C memory, strings and output (the C runtime) --------------------------------------------------------
[link("memcpy")] void* memcpy(void* dst, const void* src, uint64 n) noexcept;   // the C runtime's own signatures: a bound C header declares the same functions
[link("memmove")] void* memmove(void* dst, const void* src, uint64 n) noexcept;
[link("memset")] void* memset(void* dst, int32 value, uint64 n) noexcept;
[link("strlen")] uint64 strlen(void* s) noexcept;
[link("strcmp")] int32 strcmp(void* a, void* b) noexcept;
[link("strncmp")] int32 strncmp(void* a, void* b, uint64 n) noexcept;
[link("memcmp")] int32 memcmp(void* a, void* b, uint64 n) noexcept;
[link("memchr")] void* memchr(void* s, int32 value, uint64 n) noexcept;
[link("printf")] int32 printf(void* format, ...) noexcept;
[link("fflush")] int32 fflush(void* stream) noexcept;

// --- C math (the C runtime): the f forms take and return float, the others double ------------------------
[link("sqrtf")] float sqrtf(float x) noexcept;
[link("sinf")] float sinf(float x) noexcept;
[link("cosf")] float cosf(float x) noexcept;
[link("tanf")] float tanf(float x) noexcept;
[link("expf")] float expf(float x) noexcept;
[link("logf")] float logf(float x) noexcept;
[link("powf")] float powf(float base, float exponent) noexcept;
[link("floorf")] float floorf(float x) noexcept;
[link("ceilf")] float ceilf(float x) noexcept;
[link("fabsf")] float fabsf(float x) noexcept;
[link("asinf")] float asinf(float x) noexcept;
[link("acosf")] float acosf(float x) noexcept;
[link("atanf")] float atanf(float x) noexcept;
[link("sinhf")] float sinhf(float x) noexcept;
[link("coshf")] float coshf(float x) noexcept;
[link("tanhf")] float tanhf(float x) noexcept;
[link("roundf")] float roundf(float x) noexcept;
[link("truncf")] float truncf(float x) noexcept;
[link("log2f")] float log2f(float x) noexcept;
[link("log10f")] float log10f(float x) noexcept;
[link("atan2f")] float atan2f(float y, float x) noexcept;
[link("fmodf")] float fmodf(float x, float y) noexcept;
[link("sqrt")] double sqrt(double x) noexcept;
[link("sin")] double sin(double x) noexcept;
[link("cos")] double cos(double x) noexcept;
[link("tan")] double tan(double x) noexcept;
[link("exp")] double exp(double x) noexcept;
[link("log")] double log(double x) noexcept;
[link("pow")] double pow(double base, double exponent) noexcept;
[link("floor")] double floor(double x) noexcept;
[link("ceil")] double ceil(double x) noexcept;
[link("fabs")] double fabs(double x) noexcept;
[link("asin")] double asin(double x) noexcept;
[link("acos")] double acos(double x) noexcept;
[link("atan")] double atan(double x) noexcept;
[link("sinh")] double sinh(double x) noexcept;
[link("cosh")] double cosh(double x) noexcept;
[link("tanh")] double tanh(double x) noexcept;
[link("round")] double round(double x) noexcept;
[link("trunc")] double trunc(double x) noexcept;
[link("log2")] double log2(double x) noexcept;
[link("log10")] double log10(double x) noexcept;
[link("atan2")] double atan2(double y, double x) noexcept;
[link("fmod")] double fmod(double x, double y) noexcept;

// --- The GC heap (C4Runtime/GC) --------------------------------------------------------------------------
[link("c4_raw_alloc")] void* raw_alloc(uint64 size) noexcept;
[link("c4_raw_free")] void raw_free(void* p) noexcept;
// raw_free for a block of `bytes`, the size asked of raw_alloc: the native-memory count needs no heap lookup.
[link("c4_raw_free_sized")] void raw_free_sized(void* p, uint64 bytes) noexcept;
[link("c4_raw_alloc_aligned")] void* raw_alloc_aligned(uint64 size, uint64 align) noexcept;
[link("c4_raw_free_aligned")] void raw_free_aligned(void* p) noexcept;
// A buffer that grows in place once past 1 MiB; the owner passes back the size it asked for last (GCRuntimeC.h).
[link("c4_buffer_resize")] void* buffer_resize(void* buffer, uint64 oldBytes, uint64 keepBytes, uint64 newBytes) noexcept;
[link("c4_buffer_free")] void buffer_free(void* buffer, uint64 bytes) noexcept;
// The same for a buffer the collector scans for pointers, registered as a scan region at its whole size, zero past the
// bytes it keeps.
[link("c4_buffer_resize_scanned")] void* buffer_resize_scanned(void* buffer, uint64 oldBytes, uint64 keepBytes, uint64 newBytes) noexcept;
[link("c4_buffer_free_scanned")] void buffer_free_scanned(void* buffer, uint64 bytes) noexcept;
[link("c4_gc_collect")] void gc_collect() noexcept;
[link("c4_gc_register_region")] void gc_register_region(void* start, uint64 size) noexcept;
[link("c4_gc_unregister_region")] void gc_unregister_region(void* start) noexcept;
[link("c4_gc_allocs")] uint64 gc_allocs() noexcept;
[link("c4_gc_collections")] uint64 gc_collections() noexcept;
[link("c4_gc_heap_bytes")] uint64 gc_heap_bytes() noexcept;
[link("c4_gc_free_bytes")] uint64 gc_free_bytes() noexcept;
[link("c4_gc_register_thread")] void gc_register_thread() noexcept;

// --- Threads (C4Runtime/ThreadRuntimeC.c) ---------------------------------------------------------------
[link("c4_thread_attach")] void thread_attach() noexcept;
[link("c4_thread_start")] void* thread_start(funcptr<void(void*)> entry, void* state) noexcept;
[link("c4_thread_join")] void thread_join(void* thread) noexcept;
[link("c4_thread_detach")] void thread_detach(void* thread) noexcept;
[link("c4_thread_current_id")] uint64 thread_current_id() noexcept;
[link("c4_thread_hardware_count")] uint32 thread_hardware_count() noexcept;
[link("c4_thread_yield")] void thread_yield() noexcept;
[link("c4_cpu_relax")] void cpu_relax() noexcept;
[link("c4_thread_sleep_ns")] void thread_sleep_ns(int64 ns) noexcept;
[link("c4_thread_set_name")] int32 thread_set_name(const char* name) noexcept;
[link("c4_thread_name")] int32 thread_name(char* out, int32 cap) noexcept;
[link("c4_wait_on_address")] bool wait_on_address(void* address, void* expected, uint64 size, int64 timeoutNs) noexcept;
[link("c4_wake_one")] void wake_one(void* address) noexcept;
[link("c4_wake_all")] void wake_all(void* address) noexcept;

// --- Time (C4Runtime/ClockRuntimeC.c) --------------------------------------------------------------------
// Nanoseconds on the monotonic clock: for durations, never for the date.
[link("c4_clock_now_ns")] int64 clock_now_ns() noexcept;

// --- Bytes (C4Runtime/MemoryRuntimeC.c) ------------------------------------------------------------------
[link("c4_byte_copy")] void byte_copy(void* dst, void* src, int64 length, int64 dstOffset, int64 srcOffset) noexcept;
[link("c4_byte_move")] void byte_move(void* dst, void* src, int64 length, int64 dstOffset, int64 srcOffset) noexcept;
[link("c4_byte_at")] int32 byte_at(void* p, int64 offset) noexcept;
[link("c4_set_byte")] void set_byte(void* p, int64 offset, int32 value) noexcept;

// --- Files: a file is an opaque handle (C4Runtime/IORuntimeC.c) ------------------------------------------
[link("file_open")] void* file_open(void* path, void* mode) noexcept;
[link("file_read")] int64 file_read(void* file, void* buffer, int64 length) noexcept;
[link("file_write")] int64 file_write(void* file, void* buffer, int64 length) noexcept;
[link("file_flush")] int32 file_flush(void* file) noexcept;
[link("file_close")] int32 file_close(void* file) noexcept;
[link("file_seek")] int64 file_seek(void* file, int64 offset) noexcept;
[link("file_seek_from")] int64 file_seek_from(void* file, int64 offset, int32 origin) noexcept;
[link("file_tell")] int64 file_tell(void* file) noexcept;
[link("file_size")] int64 file_size(void* path) noexcept;
[link("file_exists")] int32 file_exists(void* path) noexcept;
[link("file_delete")] int32 file_delete(void* path) noexcept;

// --- Directories (C4Runtime/FileSystemRuntimeC.c) --------------------------------------------------------
[link("c4_fs_is_directory")] int32 fs_is_directory(void* path) noexcept;
[link("c4_fs_is_link")] int32 fs_is_link(void* path) noexcept;
[link("c4_fs_make_directory")] int32 fs_make_directory(void* path) noexcept;
[link("c4_fs_remove_directory")] int32 fs_remove_directory(void* path) noexcept;
[link("c4_fs_rename")] int32 fs_rename(void* from, void* to) noexcept;
[link("c4_fs_current_directory")] int64 fs_current_directory(void* out, int64 capacity) noexcept;
[link("c4_fs_temp_directory")] int64 fs_temp_directory(void* out, int64 capacity) noexcept;
[link("c4_fs_directory_open")] void* fs_directory_open(void* path) noexcept;
[link("c4_fs_directory_next")] int32 fs_directory_next(void* listing, void* name, int32 capacity, void* kind) noexcept;
[link("c4_fs_directory_close")] void fs_directory_close(void* listing) noexcept;

// --- Number text (C4Runtime/TextRuntimeC.c) --------------------------------------------------------------
[link("c4_format_double")] int32 format_double(void* out, int32 capacity, double value, int32 decimals) noexcept;
[link("c4_format_float")] int32 format_float(void* out, int32 capacity, float value, int32 decimals) noexcept;
[link("c4_parse_double")] int64 parse_double(void* text, int64 length, void* out) noexcept;

// --- Reflection: the registry behind Bedrock's Reflection namespace (C4Runtime/ReflectRuntimeC.c) --------
[link("reflect_type")] void* reflect_type(void* name) noexcept;
[link("reflect_type_by_index")] void* reflect_type_by_index(int32 index) noexcept;
[link("reflect_count")] uint32 reflect_count() noexcept;
[link("c4_reflect_unload_source")] uint32 c4_reflect_unload_source(void* tag);
[link("reflect_name")] const char* reflect_name(void* handle) noexcept;
[link("reflect_type_tag")] const char* reflect_type_tag(void* handle) noexcept;
[link("reflect_size")] uint64 reflect_size(void* handle) noexcept;
[link("reflect_align")] uint64 reflect_align(void* handle) noexcept;
[link("reflect_field_count")] uint32 reflect_field_count(void* handle) noexcept;
[link("reflect_method_count")] uint32 reflect_method_count(void* handle) noexcept;
[link("reflect_enum_count")] uint32 reflect_enum_count(void* handle) noexcept;
[link("reflect_base_name")] const char* reflect_base_name(void* handle) noexcept;
[link("reflect_field_handle_type_name")] const char* reflect_field_handle_type_name(void* handle) noexcept;
[link("reflect_enum_at")] void* reflect_enum_at(void* handle, uint32 index) noexcept;
[link("reflect_enum_handle_name")] const char* reflect_enum_handle_name(void* handle) noexcept;
[link("reflect_enum_handle_value")] int64 reflect_enum_handle_value(void* handle) noexcept;
[link("reflect_is_virtual_type")] int32 reflect_is_virtual_type(void* handle) noexcept;
[link("reflect_type_flags")] uint32 reflect_type_flags(void* handle) noexcept;
[link("reflect_type_has_destroy")] uint32 reflect_type_has_destroy(void* handle) noexcept;
[link("reflect_type_has_copy")] uint32 reflect_type_has_copy(void* handle) noexcept;
[link("reflect_type_construct")] uint32 reflect_type_construct(void* handle, void* object);
[link("reflect_type_destroy")] void reflect_type_destroy(void* handle, void* object);
[link("reflect_type_copy")] uint32 reflect_type_copy(void* handle, void* to, const void* from);
[link("reflect_variable")] void* reflect_variable(void* name) noexcept;
[link("reflect_function")] void* reflect_function(void* name) noexcept;
[link("reflect_function_in")] void* reflect_function_in(void* tag, void* name) noexcept;
[link("reflect_function_symbol_in")] const char* reflect_function_symbol_in(void* tag, void* name) noexcept;
[link("reflect_variable_count")] uint32 reflect_variable_count() noexcept;
[link("reflect_function_count")] uint32 reflect_function_count() noexcept;
[link("reflect_variable_by_index")] void* reflect_variable_by_index(int32 index) noexcept;
[link("reflect_function_by_index")] void* reflect_function_by_index(int32 index) noexcept;
[link("reflect_function_containing")] void* reflect_function_containing(const void* address) noexcept;
[link("reflect_field")] void* reflect_field(void* type, void* name) noexcept;
[link("reflect_method")] void* reflect_method(void* type, void* name) noexcept;
[link("reflect_field_at")] void* reflect_field_at(void* handle, uint32 index) noexcept;
[link("reflect_method_at")] void* reflect_method_at(void* handle, uint32 index) noexcept;
[link("reflect_method_param")] void* reflect_method_param(void* handle, uint32 index) noexcept;
[link("reflect_field_type_handle")] void* reflect_field_type_handle(void* handle) noexcept;
[link("reflect_method_return_handle")] void* reflect_method_return_handle(void* handle) noexcept;
[link("reflect_param_type_handle")] void* reflect_param_type_handle(void* handle) noexcept;
[link("reflect_variable_type_handle")] void* reflect_variable_type_handle(void* handle) noexcept;
[link("reflect_variable_address")] void* reflect_variable_address(void* handle) noexcept;
[link("reflect_group")] void* reflect_group(void* name) noexcept;
[link("reflect_group_handle_count")] uint32 reflect_group_handle_count(void* handle) noexcept;
[link("reflect_group_handle_item")] void* reflect_group_handle_item(void* handle, uint32 index) noexcept;
[link("reflect_group_name_count")] uint32 reflect_group_name_count() noexcept;
[link("reflect_group_name_at")] const char* reflect_group_name_at(uint32 index) noexcept;
[link("reflect_kind_of")] uint32 reflect_kind_of(void* handle) noexcept;
[link("reflect_field_handle_name")] const char* reflect_field_handle_name(void* handle) noexcept;
[link("reflect_field_handle_offset")] uint64 reflect_field_handle_offset(void* handle) noexcept;
[link("reflect_field_handle_size")] uint64 reflect_field_handle_size(void* handle) noexcept;
[link("reflect_method_handle_name")] const char* reflect_method_handle_name(void* handle) noexcept;
[link("reflect_method_handle_param_count")] uint32 reflect_method_handle_param_count(void* handle) noexcept;
[link("reflect_method_handle_is_virtual")] int32 reflect_method_handle_is_virtual(void* handle) noexcept;
[link("reflect_variable_handle_name")] const char* reflect_variable_handle_name(void* handle) noexcept;
[link("reflect_function_handle_name")] const char* reflect_function_handle_name(void* handle) noexcept;
[link("reflect_variable_handle_tag")] const char* reflect_variable_handle_tag(void* handle) noexcept;
[link("reflect_function_handle_tag")] const char* reflect_function_handle_tag(void* handle) noexcept;
[link("reflect_function_handle_param_count")] uint32 reflect_function_handle_param_count(void* handle) noexcept;
[link("reflect_field_handle_is_const")] uint32 reflect_field_handle_is_const(void* handle) noexcept;
[link("reflect_field_handle_is_alias")] uint32 reflect_field_handle_is_alias(void* handle) noexcept;
[link("reflect_method_handle_is_static")] uint32 reflect_method_handle_is_static(void* handle) noexcept;
[link("reflect_method_handle_is_const")] uint32 reflect_method_handle_is_const(void* handle) noexcept;
[link("reflect_method_handle_return_type_name")] const char* reflect_method_handle_return_type_name(void* handle) noexcept;
[link("reflect_method_handle_operator_role")] const char* reflect_method_handle_operator_role(void* handle) noexcept;
[link("reflect_param_handle_name")] const char* reflect_param_handle_name(void* handle) noexcept;
[link("reflect_param_handle_type_name")] const char* reflect_param_handle_type_name(void* handle) noexcept;
[link("reflect_variable_handle_type_name")] const char* reflect_variable_handle_type_name(void* handle) noexcept;
[link("reflect_variable_handle_is_const")] uint32 reflect_variable_handle_is_const(void* handle) noexcept;
[link("reflect_variable_handle_owner")] const char* reflect_variable_handle_owner(void* handle) noexcept;
[link("reflect_function_handle_symbol")] const char* reflect_function_handle_symbol(void* handle) noexcept;
[link("reflect_function_handle_address")] const void* reflect_function_handle_address(void* handle) noexcept;
[link("reflect_function_handle_is_static")] uint32 reflect_function_handle_is_static(void* handle) noexcept;
[link("reflect_function_handle_owner")] const char* reflect_function_handle_owner(void* handle) noexcept;
[link("reflect_function_handle_return_type_name")] const char* reflect_function_handle_return_type_name(void* handle) noexcept;
[link("reflect_function_param")] void* reflect_function_param(void* handle, uint32 index) noexcept;
[link("reflect_function_return_handle")] void* reflect_function_return_handle(void* handle) noexcept;
[link("reflect_function_is_invokable")] uint32 reflect_function_is_invokable(void* handle) noexcept;
[link("reflect_method_is_invokable")] uint32 reflect_method_is_invokable(void* handle) noexcept;
[link("reflect_function_invoke")] uint32 reflect_function_invoke(void* function, void* object, void* result, void* arguments);
[link("reflect_method_invoke")] uint32 reflect_method_invoke(void* method, void* object, void* result, void* arguments);
[link("reflect_type_annotation_count")] uint32 reflect_type_annotation_count(void* handle) noexcept;
[link("reflect_type_annotation_at")] void* reflect_type_annotation_at(void* handle, uint32 index) noexcept;
[link("reflect_field_annotation_count")] uint32 reflect_field_annotation_count(void* handle) noexcept;
[link("reflect_field_annotation_at")] void* reflect_field_annotation_at(void* handle, uint32 index) noexcept;
[link("reflect_method_annotation_count")] uint32 reflect_method_annotation_count(void* handle) noexcept;
[link("reflect_method_annotation_at")] void* reflect_method_annotation_at(void* handle, uint32 index) noexcept;
[link("reflect_param_annotation_count")] uint32 reflect_param_annotation_count(void* handle) noexcept;
[link("reflect_param_annotation_at")] void* reflect_param_annotation_at(void* handle, uint32 index) noexcept;
[link("reflect_variable_annotation_count")] uint32 reflect_variable_annotation_count(void* handle) noexcept;
[link("reflect_variable_annotation_at")] void* reflect_variable_annotation_at(void* handle, uint32 index) noexcept;
[link("reflect_function_annotation_count")] uint32 reflect_function_annotation_count(void* handle) noexcept;
[link("reflect_function_annotation_at")] void* reflect_function_annotation_at(void* handle, uint32 index) noexcept;
[link("reflect_annotation_name")] const char* reflect_annotation_name(void* handle) noexcept;
[link("reflect_annotation_arg_count")] uint32 reflect_annotation_arg_count(void* handle) noexcept;
[link("reflect_annotation_arg_kind")] uint32 reflect_annotation_arg_kind(void* handle, uint32 index) noexcept;
[link("reflect_annotation_arg_int")] int64 reflect_annotation_arg_int(void* handle, uint32 index) noexcept;
[link("reflect_annotation_arg_float")] double reflect_annotation_arg_float(void* handle, uint32 index) noexcept;
[link("reflect_annotation_arg_string")] const char* reflect_annotation_arg_string(void* handle, uint32 index) noexcept;
[link("c4_reflect_on_unload")] uint32 c4_reflect_on_unload(void* tag, void* callback, void* user) noexcept;
[link("c4_reflect_remove_listener")] void c4_reflect_remove_listener(uint32 id) noexcept;
[link("reflect_type_static_variable_count")] uint32 reflect_type_static_variable_count(void* handle) noexcept;
[link("reflect_type_static_variable_at")] void* reflect_type_static_variable_at(void* handle, uint32 index) noexcept;

// --- Test assertions (C4Runtime/TestRuntimeC.c): the compiler calls the _at forms with the call site ------
[link("assert_true")] int32 assert_true(int32 condition, void* message) noexcept;
[link("assert_eq")] int32 assert_eq(int64 expected, int64 actual, void* message) noexcept;
[link("assert_feq")] int32 assert_feq(double expected, double actual, double tolerance, void* message) noexcept;
[link("assert_se")] int32 assert_se(void* expected, void* actual, void* message) noexcept;
[link("c4_assert_true_at")] int32 c4_assert_true_at(int32 condition, void* message, void* file, int32 line) noexcept;
[link("c4_assert_eq_at")] int32 c4_assert_eq_at(int64 expected, int64 actual, void* message, void* file, int32 line) noexcept;
[link("c4_assert_feq_at")] int32 c4_assert_feq_at(double expected, double actual, double tolerance, void* message, void* file, int32 line) noexcept;
[link("c4_assert_se_at")] int32 c4_assert_se_at(void* expected, void* actual, void* message, void* file, int32 line) noexcept;

// --- Atomics: the ordering an atomic_* intrinsic gives its access (docs/language/14 10) -------------------
enum class MemoryOrder : int32 { Relaxed, Acquire, Release, AcqRel, SeqCst }

// --- COM ------------------------------------------------------------------------------------------
// The layout of the Windows SDK's GUID (guiddef.h); __uuidof(T) points at one.
struct _GUID {
	uint32 Data1;
	uint16 Data2;
	uint16 Data3;
	uint8[8] Data4;
}

// --- Casts ------------------------------------------------------------------------------------------
// What `x as T&` and `dynamic_cast<T&>(x)` throw when the object is not a T (D-11).
class BadCast {
	public const char* target; // the type the cast asked for
}

// A checked cast to a reference that found no T: the compiler calls this.
void* __c4_bad_cast(const char* target) { throw BadCast{ target }; }

// --- Arrays on the heap -----------------------------------------------------------------------------
// What `new T[n]` throws when n is negative, too large to allocate, or smaller than its braced entries.
class BadArrayLength {
	public int64 length;  // the count asked for
	public int64 minimum; // the entries the braces give
}

// A `new T[n]` whose count cannot hold its elements: the compiler calls this.
void __c4_bad_array_length(int64 length, int64 minimum) { throw BadArrayLength{ length, minimum }; }
