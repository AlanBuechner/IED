#pragma once
// Support for scripts: C4 code compiled and linked into a running program (docs/runtime/04). The script host
// (C4Script.dll) and the code it compiles call these; a program that runs no scripts never does.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- a script's exits (ScriptExitsC.c) ---------------------------------------------------------------
// What a script registers to destroy: its globals and static locals, and its thread_locals on each thread.
typedef struct c4_script_module c4_script_module;
c4_script_module* c4_script_module_create(void);
// `destroy(object)` runs when the script unloads, the last registered first (a global or static local).
void c4_script_at_exit(c4_script_module* module, void (*destroy)(void*), void* object);
// `destroy(object)` runs when the calling thread ends or the script unloads, whichever comes first (a
// thread_local). A script's thread_locals on one thread end together, the last started first.
void c4_script_thread_at_exit(c4_script_module* module, void (*destroy)(void*), void* object);
// Runs every exit of the script: each thread's thread_local destructions, then its globals', each list the
// last registered first. Nothing registered later runs.
void c4_script_module_finish(c4_script_module* module);
// Frees the module; call it after c4_script_module_finish.
void c4_script_module_destroy(c4_script_module* module);

// --- emulated thread storage (ScriptTlsC.c) ------------------------------------------------------------
// A JIT image has no PE TLS directory, so script code reaches its thread_locals through LLVM's emulated TLS:
// one control per variable, and each thread's object made on its first use (zeroed, or copied from `value`).
// Every object is a GC root while it lives; a thread's objects are freed when it ends.
typedef struct __emutls_control
{
	size_t size;
	size_t align;
	uintptr_t index;   // 0 until the variable is first used; then its slot + 1
	void* value;       // the initial bytes, or null for zero
} __emutls_control;
void* __emutls_get_address(__emutls_control* control);
// Frees every thread's object of each control (a script's thread_locals as it unloads, after its exits ran).
void c4_script_tls_release(__emutls_control* const* controls, uint32_t count);

// --- images and throwing (ScriptThrowC.c) ------------------------------------------------------------
// Where a script's code and data were loaded: the exception tables in it are relative to `base`.
void c4_script_image_add(const void* base, const void* end);
void c4_script_image_remove(const void* base);
// _CxxThrowException for script code: raises the C++ exception with the base of the image holding
// `throwInfo`, which RtlPcToFileHeader cannot find for a JIT image. Null, null rethrows.
__declspec(noreturn) void c4_script_throw(void* object, const void* throwInfo);

#ifdef __cplusplus
}
#endif
