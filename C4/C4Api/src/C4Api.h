#pragma once
// The C4 compiler's C API (docs/tools/06-c4-api.md): C4Api.dll gives tools around C4 - written in C, C++, C4 through
// interop, or any language that calls C - what the compiler knows about a source, behind a C interface that keeps its
// shape across compiler versions. Every toolchain ships it in C4Api/ (this header, C4Api.dll, its import library
// C4Api.lib); a Nuke project depends on it by name (depends = ["C4Api"]). Strings are UTF-8 and owned by the DLL: one
// returned stays valid until the object it came from is destroyed.

#include <stdint.h>

#ifdef C4API_EXPORTS
#define C4API __declspec(dllexport)
#else
#define C4API __declspec(dllimport)
#endif

// The API this header declares; c4a_api_version() is the one the DLL implements, and a DLL implements every version
// before its own.
#define C4A_API_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct c4a_parse c4a_parse;

typedef enum c4a_severity { C4A_WARNING = 0, C4A_ERROR = 1 } c4a_severity;

typedef enum c4a_kind
{
	C4A_NAMESPACE = 0, C4A_CLASS = 1, C4A_STRUCT = 2, C4A_INTERFACE = 3, C4A_ENUM = 4, C4A_ENUM_VALUE = 5,
	C4A_FUNCTION = 6, C4A_VARIABLE = 7, C4A_UNION = 8, C4A_ALIAS = 9
} c4a_kind;

// A place in the source: a 1-based line, and a 1-based column in bytes.
typedef struct c4a_position
{
	uint32_t line;
	uint32_t column;
} c4a_position;

typedef struct c4a_diagnostic
{
	c4a_severity severity;
	c4a_position at;
	const char* message;
} c4a_diagnostic;

typedef struct c4a_declaration
{
	c4a_kind kind;
	const char* name;      // as written: "Registry::Add" for a member defined out of its type
	c4a_position start;    // its first token
	c4a_position end;      // after its last token
	c4a_position name_at;  // its name
	int32_t parent;        // the index of the declaration it is in; -1 at file level
} c4a_declaration;

// What to parse. `text` with `length` bytes is the source; a null `text` reads the file at `path`.
typedef struct c4a_parse_options
{
	const char* path;
	const char* text;
	uint64_t length;
	const char* const* defines;   // NAME or NAME=VALUE, as after C4.exe -D (a NAME alone is 1)
	uint32_t define_count;
} c4a_parse_options;

C4API int32_t c4a_api_version(void);
// The version of the compiler the DLL was built from ("0.1.0").
C4API const char* c4a_compiler_version(void);

// Parses one C4 source with the macro preprocessor and the parser alone - no include is read, no name resolved - into
// its declarations, depth first, each before what it holds, and its syntax diagnostics. Never null: when the file
// cannot be read, c4a_parse_read is 0 and the one diagnostic says why.
C4API c4a_parse* c4a_parse_source(const c4a_parse_options* options);
C4API int32_t c4a_parse_read(const c4a_parse* parse);
C4API uint32_t c4a_diagnostic_count(const c4a_parse* parse);
// The diagnostic at `index` into `out`; 0 when there is none.
C4API int32_t c4a_diagnostic_at(const c4a_parse* parse, uint32_t index, c4a_diagnostic* out);
C4API uint32_t c4a_declaration_count(const c4a_parse* parse);
// The declaration at `index` into `out`; 0 when there is none.
C4API int32_t c4a_declaration_at(const c4a_parse* parse, uint32_t index, c4a_declaration* out);
// The name of a kind: "namespace", "class", ..., "alias"; "" for another value.
C4API const char* c4a_kind_name(c4a_kind kind);
C4API void c4a_parse_destroy(c4a_parse* parse);

#ifdef __cplusplus
}
#endif
