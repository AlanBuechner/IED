#pragma once
// The script host's C API (docs/runtime/04): C4Script.dll compiles, links and loads C4 scripts into a program built
// with --script-host. Bedrock's Scripting namespace (Bedrock/Scripting/scripting.ch) is its C4 face. Strings are
// UTF-8 and owned by the DLL; a returned one stays valid until the object it came from is destroyed.

#include <stdint.h>

#ifdef C4SCRIPT_EXPORTS
#define C4SCRIPT_API __declspec(dllexport)
#else
#define C4SCRIPT_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct c4s_host c4s_host;
typedef struct c4s_script c4s_script;

// What stopped a guarded call (C4Runtime's c4_script_fault).
typedef struct c4s_fault
{
	uint32_t code;     // the exception code (EXCEPTION_ACCESS_VIOLATION, ...)
	uint32_t write;    // an access violation: 1 for a write, 8 for an execution, 0 for a read
	uint64_t address;  // the faulting instruction
	uint64_t target;   // an access violation: the address it touched
} c4s_fault;

// What to compile. A file with a null text is read from disk at its path; one with a text is given whole.
typedef struct c4s_options
{
	const char* name;                // the script's name and its reflection tag
	const char* const* paths;        // fileCount paths
	const char* const* texts;        // null, or fileCount texts (a null entry: read the path)
	uint32_t fileCount;
	const char* const* allow;        // the host headers a script may include (projects or path prefixes); none: all
	uint32_t allowCount;
	const char* const* defines;      // NAME or NAME=VALUE, as after -D
	uint32_t defineCount;
	int32_t optimize;                // 1: -O2, 0: -O0
} c4s_options;

// The program's script host: its interface at `interfacePath` (null: <the executable>.c4i) against its symbol table
// (`table`, the program's __c4_script_table). Null on failure, with the reason in `error` (up to errorSize bytes).
C4SCRIPT_API c4s_host* c4s_host_open(const char* interfacePath, const void* table, char* error, uint64_t errorSize);
C4SCRIPT_API void c4s_host_close(c4s_host* host);
// Keeps compiled scripts in `folder` from now on (docs/runtime/04 section 4.6): a script compiled again with the same
// files, options, host build and compiler links the machine code kept from before. 0 when it cannot.
C4SCRIPT_API int32_t c4s_host_use_cache(c4s_host* host, const char* folder);

// Compiles, links and loads a script. Never null: c4s_script_ok says whether it worked, the diagnostics why not.
C4SCRIPT_API c4s_script* c4s_compile(c4s_host* host, const c4s_options* options);
// Compiles `options` to replace `old` (docs/runtime/04 section 5): when it compiles and links, `old` unloads and then
// the new script loads; when it does not, `old` stays loaded and the result has the diagnostics. Never null.
C4SCRIPT_API c4s_script* c4s_reload(c4s_host* host, c4s_script* old, const c4s_options* options);
C4SCRIPT_API int32_t c4s_script_ok(const c4s_script* script);
C4SCRIPT_API const char* c4s_script_diagnostics(const c4s_script* script);
C4SCRIPT_API const char* c4s_script_tag(const c4s_script* script);
// Whether the script was linked from the host's cache (c4s_host_use_cache) instead of compiled.
C4SCRIPT_API int32_t c4s_script_cached(const c4s_script* script);
// Whether a guarded call into the script faulted, and what stopped it ("" when nothing did).
C4SCRIPT_API int32_t c4s_script_faulted(const c4s_script* script);
C4SCRIPT_API const char* c4s_script_fault(c4s_script* script);
// The address of the script's symbol, or null.
C4SCRIPT_API void* c4s_script_find(const c4s_script* script, const char* symbol);
// Unloads the script (docs/runtime/04 section 5) and returns how many finalizers of its objects still alive it
// dropped. The script stays an object to destroy.
C4SCRIPT_API uint64_t c4s_script_unload(c4s_script* script);
// Unloads it when it is still loaded, then frees it.
C4SCRIPT_API void c4s_script_destroy(c4s_script* script);

#ifdef __cplusplus
}

// These two let a C++ or C4 exception thrown in `fn` pass through, so they have C++ linkage under their C names:
// Frost compiles with -fexternc-nounwind, which gives an extern "C" function no unwind table.

// Runs fn(context) guarded; 0 when it faulted (the script is then faulted and refuses further calls).
C4SCRIPT_API int32_t c4s_script_call(c4s_script* script, void (*fn)(void*), void* context) __asm__("c4s_script_call");
// Runs fn(context); a hardware fault stops the call: 0, with the fault.
C4SCRIPT_API int32_t c4s_guarded_call(void (*fn)(void*), void* context, c4s_fault* fault) __asm__("c4s_guarded_call");
#endif
