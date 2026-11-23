#pragma once
// The C4 compiler's C API (docs/tools/06-c4-api.md): C4Api.dll gives tools around C4 - written in C, C++, C4 through
// interop, or any language that calls C - what the compiler knows about a source, behind a C interface that keeps its
// shape across compiler versions. Every toolchain ships it in C4Api/ (this header, C4Api.dll, its import library
// C4Api.lib); a Nuke project depends on it by name (depends = ["C4Api"]). Strings are UTF-8 and owned by the DLL: one
// returned stays valid until the object it came from is destroyed.

#include <stdint.h>

#if !defined(_WIN32)
#define C4API __attribute__((visibility("default"))) // C4Api.so on Linux: its exports are its default-visible symbols
#elif defined(C4API_EXPORTS)
#define C4API __declspec(dllexport)
#else
#define C4API __declspec(dllimport)
#endif

// The API this header declares; c4a_api_version() is the one the DLL implements, and a DLL implements every version
// before its own.
#define C4A_API_VERSION 2

#ifdef __cplusplus
extern "C" {
#endif

typedef struct c4a_parse c4a_parse;

typedef enum c4a_severity { C4A_WARNING = 0, C4A_ERROR = 1 } c4a_severity;

typedef enum c4a_kind
{
	C4A_NAMESPACE = 0, C4A_CLASS = 1, C4A_STRUCT = 2, C4A_INTERFACE = 3, C4A_ENUM = 4, C4A_ENUM_VALUE = 5,
	C4A_FUNCTION = 6, C4A_VARIABLE = 7, C4A_UNION = 8, C4A_ALIAS = 9, C4A_ANNOTATION = 10
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

// ---- Version 2: a whole program, analyzed and queried ------------------------------------------------------------------
// What C4.exe and the language server know of a program: every diagnostic of its front end and (when that is clean) of
// its bodies, what a name at a place resolves to, the type of an expression, a declaration's references, a type's members.
// Files are named as the program names them: a path (relative ones are read from `root`) or the name of an in-memory
// source. Positions are 1-based lines and 1-based byte columns. A query that finds nothing answers 0 (or null); the
// reason of a program that could not be analyzed is c4a_program_error. Strings stay valid until the program (or the
// result handle that gave them) is destroyed. A program serializes its own queries; separate programs run in parallel.

typedef struct c4a_program c4a_program;
typedef struct c4a_locations c4a_locations;
typedef struct c4a_members c4a_members;

// An in-memory source: `name` as a file would be named ("main.cf"), `length` bytes of `text`.
typedef struct c4a_source
{
	const char* name;
	const char* text;
	uint64_t length;
} c4a_source;

// The program to open. Its files are, in this order of preference: the projects `projects_file` describes (a Nuke
// projects description, C4.exe's --projects), else the C4 files under `root`; the in-memory `sources` come on top.
// `library_folder` is the C4 install's default library (Bedrock's folder), where `include "bedrock.ch"` is read
// from; null for a program that includes none.
typedef struct c4a_program_options
{
	const char* root;
	const char* projects_file;
	const char* library_folder;
	const char* const* include_paths;
	uint32_t include_path_count;
	const char* const* defines;        // NAME or NAME=VALUE, as after C4.exe -D
	uint32_t define_count;
	const c4a_source* sources;
	uint32_t source_count;
} c4a_program_options;

// A place: a file and where in it.
typedef struct c4a_location
{
	const char* file;
	c4a_position start;
	c4a_position end;
} c4a_location;

typedef struct c4a_program_diagnostic
{
	c4a_severity severity;
	const char* file;   // "" for one that belongs to no file
	c4a_position at;
	const char* message;
} c4a_program_diagnostic;

// What a name resolves to: the declaration it names.
typedef struct c4a_symbol
{
	c4a_kind kind;
	const char* name;      // qualified: "Game::Ring::Push"
	const char* detail;    // the declaration as written, qualified with its owner
	const char* type;      // the type of the variable or parameter, the type a type name denotes; "" when it has none
	c4a_location declared; // where its name is written
} c4a_symbol;

typedef struct c4a_member
{
	c4a_kind kind;         // C4A_VARIABLE for a field, C4A_FUNCTION for a method
	const char* name;
	const char* signature; // the declaration without its name: "static int64 (const StringView& text) const"
	const char* access;    // "public", "private", "protected"
	int32_t is_static;
	c4a_location declared;
} c4a_member;

// Opens the program and analyzes it: the front end (preprocess, parse, register, the model) and, when that finds no
// error, the lowering of the bodies - as C4.exe does - for the body diagnostics. Never null.
C4API c4a_program* c4a_program_open(const c4a_program_options* options);
// Why the program could not be analyzed (an unreadable projects file, ...); null when it was. Program errors in the
// sources are diagnostics, not this.
C4API const char* c4a_program_error(const c4a_program* program);
// Whether the bodies were lowered: 0 when the front end found an error, so no body diagnostics are there.
C4API int32_t c4a_program_bodies_lowered(const c4a_program* program);
C4API void c4a_program_destroy(c4a_program* program);

// Every diagnostic of the program: the front end's, then the bodies', each once.
C4API uint32_t c4a_program_diagnostic_count(const c4a_program* program);
C4API int32_t c4a_program_diagnostic_at(const c4a_program* program, uint32_t index, c4a_program_diagnostic* out);

// The declaration the name at `at` of `file` resolves to (a reference's declaration, or the declaration whose own name
// is there); 0 when there is none.
C4API int32_t c4a_program_resolve(c4a_program* program, const char* file, c4a_position at, c4a_symbol* out);
// The type of the expression or declaration at `at`, as text ("int32", "Point*"); null when it has none.
C4API const char* c4a_program_type_at(c4a_program* program, const char* file, c4a_position at);

// Every use of the declaration at `at`, and with `include_declaration` its own names first; null when `at` names nothing.
C4API c4a_locations* c4a_program_references(c4a_program* program, const char* file, c4a_position at, int32_t include_declaration);
C4API uint32_t c4a_locations_count(const c4a_locations* locations);
C4API int32_t c4a_locations_at(const c4a_locations* locations, uint32_t index, c4a_location* out);
C4API void c4a_locations_destroy(c4a_locations* locations);

// The fields and methods a type declares (not its bases'): the type named `qualified_name` ("Game::Ring"), or the type
// the name at `at` denotes (a type's name, a variable of the type). Null when there is no such object type.
C4API c4a_members* c4a_program_members_of(c4a_program* program, const char* qualified_name);
C4API c4a_members* c4a_program_members_at(c4a_program* program, const char* file, c4a_position at);
C4API uint32_t c4a_members_count(const c4a_members* members);
C4API int32_t c4a_members_at(const c4a_members* members, uint32_t index, c4a_member* out);
C4API void c4a_members_destroy(c4a_members* members);

#ifdef __cplusplus
}
#endif
