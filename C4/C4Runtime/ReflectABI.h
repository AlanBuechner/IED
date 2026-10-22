// ReflectABI.h - the C4 reflection wire format, shared by BOTH sides of the boundary:
//   * the compiler (Backend/Reflection/ReflectionTables mirrors these structs field for field and emits a program's
//     tables as one c4_reflect_image constant) and
//   * the runtime (ReflectRuntimeC.c copies them into owned records and answers queries).
// Keep the two in step: a field added here is a field added there.
//
// The registration `kind` is an INDEPENDENT stable namespace owned by this ABI - it is NOT the
// compiler-internal TypeKind enum. Values are part of the ABI - do not renumber.
//
// Identity (D-50): a type registers under its fully qualified, argument-aware name (`Geo::Box<int32>`),
// a variable or function under its emitted symbol, which is unique; names are looked up through an index.
// Every string is copied by the runtime, so the tables may live in an image that is later unloaded.

#ifndef C4_REFLECT_ABI_H
#define C4_REFLECT_ABI_H

#include <stdint.h>

#define reflect_KIND_OBJECT   14
#define reflect_KIND_ENUM     15
#define reflect_KIND_UNION    16
#define reflect_KIND_VARIABLE 17
#define reflect_KIND_FUNCTION 18

#define reflect_ARG_INT    1
#define reflect_ARG_FLOAT  2
#define reflect_ARG_BOOL   3
#define reflect_ARG_STRING 4

// One argument of a reflected annotation, folded at compile time.
typedef struct c4_annotation_arg {
    uint32_t kind;                 // reflect_ARG_*
    int64_t intValue;              // INT, and BOOL as 0 or 1
    double floatValue;             // FLOAT
    const char* stringValue;       // STRING (UTF-8), "" for the other kinds
} c4_annotation_arg;

// An annotation the compiler does not act on (`[Range(0, 1)]`): its name as written and its arguments.
typedef struct c4_annotation_info {
    const char* name;              // qualified as written (`Game::Serialize`)
    uint32_t argCount;
    const c4_annotation_arg* args;
} c4_annotation_info;

// A function's invoke thunk (D-50): `code` is the function a thunk shared by its signature calls (the record's
// `code`; unused by a thunk of its own), `object` is a method's receiver (unused otherwise), `arguments[i]` the
// address of argument i as the parameter's type, and the result is built in `result` (a reference result
// stores the referent's address there; unused for void).
typedef void (*c4_invoke_fn)(const void* code, void* object, void* result, void** arguments);

// One parameter of a reflected function or method.
typedef struct c4_param_info {
    const char* name;              // "" when unnamed
    const char* typeName;
    uint32_t annotationCount;      // the annotations the compiler does not act on (D-42)
    const c4_annotation_info* annotations;
} c4_param_info;

// One data member of a reflected object type.
typedef struct c4_field_info {
    const char* name;
    const char* typeName;
    uint64_t size;
    uint64_t offset;               // from the object's start, inherited fields included
    uint32_t isConst;
    uint32_t isAlias;              // names bytes an earlier field of the same union already names
    uint32_t annotationCount;      // the annotations the compiler does not act on (D-42)
    const c4_annotation_info* annotations;
} c4_field_info;

// One method of a reflected object type (static methods included).
typedef struct c4_method_info {
    const char* name;
    uint32_t paramCount;           // excluding the implicit this
    const c4_param_info* params;
    const char* returnType;
    uint32_t isStatic;
    uint32_t isVirtual;
    uint32_t isConst;
    c4_invoke_fn invoke;           // NULL when reflection cannot call it
    uint32_t annotationCount;      // the annotations the compiler does not act on (D-42)
    const c4_annotation_info* annotations;
    const void* code;              // what a shared invoke thunk calls; NULL for a thunk of its own
} c4_method_info;

// One enumerator of a reflected enum.
typedef struct c4_enum_value {
    const char* name;
    int64_t value;
} c4_enum_value;

// A reflected object type or enum.
typedef struct c4_type_info {
    uint32_t kind;                 // reflect_KIND_OBJECT / reflect_KIND_ENUM / reflect_KIND_UNION
    const char* name;              // fully qualified, with template arguments
    uint64_t size;
    uint64_t align;
    uint32_t fieldCount;
    const c4_field_info* fields;
    uint32_t methodCount;
    const c4_method_info* methods;
    const char* baseName;          // primary base type name, or ""
    uint32_t enumValueCount;
    const c4_enum_value* enumValues;
    uint32_t groupCount;           // [group("name")] annotations on the declaration
    const char* const* groups;
    uint32_t annotationCount;      // the annotations the compiler does not act on (D-42)
    const c4_annotation_info* annotations;
} c4_type_info;

// A global variable or static data member.
typedef struct c4_variable_info {
    const char* name;              // qualified C4 name (`Ns::g_hp`, `Type::s_count`)
    const char* symbol;            // emitted symbol: the variable's identity
    const char* typeName;
    uint64_t size;
    uint64_t align;
    uint32_t isConst;
    const char* ownerType;         // the type of a static data member, "" for a global
    uint32_t groupCount;
    const char* const* groups;
    uint32_t annotationCount;      // the annotations the compiler does not act on (D-42)
    const c4_annotation_info* annotations;
} c4_variable_info;

// A free function or method (methods also appear in their type's method table).
typedef struct c4_function_info {
    const char* name;              // qualified for a free function (`Ns::Add`), plain for a method
    const char* symbol;            // emitted symbol: the function's identity, so overloads are distinct entries
    const char* returnType;
    uint32_t paramCount;           // excluding the implicit this
    const c4_param_info* params;
    uint32_t isStatic;             // 1 for static methods and free functions
    uint32_t isVariadic;
    const char* ownerType;         // the owning type of a method, "" for a free function
    uint32_t groupCount;
    const char* const* groups;
    c4_invoke_fn invoke;           // NULL when reflection cannot call it
    uint32_t annotationCount;      // the annotations the compiler does not act on (D-42)
    const c4_annotation_info* annotations;
    const void* code;              // what a shared invoke thunk calls; NULL for a thunk of its own
    const void* address;           // the function's own code; NULL when reflection cannot call it
} c4_function_info;

// Everything one image registers: a program's (or a script's) types, its variables with their addresses, and its
// functions, as the compiler emits them (`__c4_reflect_image`).
typedef struct c4_reflect_image {
    uint32_t typeCount;
    const c4_type_info* types;
    uint32_t variableCount;
    const c4_variable_info* variables;
    void* const* addresses;        // variables[i]'s storage
    uint32_t functionCount;
    const c4_function_info* functions;
} c4_reflect_image;

// --- registration (a program's startup and script loaders call these) ---------------------------------
// `tag` is the unit of hot reload ("" for the executable). Registering an existing identity replaces the
// record in place, so handles stay valid. Each returns the stable handle.
void                    c4_reflect_register_image(const char* tag, const c4_reflect_image* image);
const c4_type_info*     c4_reflect_register_type(const char* tag, const c4_type_info* info);
const c4_variable_info* c4_reflect_register_variable(const char* tag, const c4_variable_info* info, void* address);
const c4_function_info* c4_reflect_register_function(const char* tag, const c4_function_info* info);
uint32_t                c4_reflect_unload_source(const char* tag);  // notify, then remove
// The two halves, for a loader that ends a script between them (docs/runtime/04 section 5): the tag's listeners
// run while its entities are registered and its objects alive; then its entities go.
void                    c4_reflect_notify_unload(const char* tag);
uint32_t                c4_reflect_remove_source(const char* tag);

// `fn(tag, user)` runs when c4_reflect_unload_source(tag) starts, while that tag's records are still valid;
// a listener for "" hears every tag. Returns the listener's id, 0 when it could not be added.
typedef void (*c4_unload_fn)(const char* tag, void* user);
uint32_t c4_reflect_on_unload(const char* tag, c4_unload_fn fn, void* user);
void     c4_reflect_remove_listener(uint32_t id);

// --- registry lookups (NULL when unknown) ---------------------------------------------------------------
const c4_type_info*     reflect_type(const char* name);
const c4_variable_info* reflect_variable(const char* name);
const c4_function_info* reflect_function(const char* name);   // the first registered overload
// The free function `name` registered under `tag`, as lookups show it (in front); null when another tag's
// registration of it is in front, or `tag` registered none.
const c4_function_info* reflect_function_in(const char* tag, const char* name);
// The symbol of the free function `name` that `tag` registered, in front or hidden behind a later registration of
// it; null when `tag` registered none. Valid until `tag` unloads.
const char* reflect_function_symbol_in(const char* tag, const char* name);
uint32_t reflect_count(void);
uint32_t reflect_variable_count(void);
uint32_t reflect_function_count(void);
const c4_type_info*     reflect_type_by_index(uint32_t i);
const c4_variable_info* reflect_variable_by_index(uint32_t i);
const c4_function_info* reflect_function_by_index(uint32_t i);
// The function whose code holds `address` (a return address of a sampled stack, say), found by the unwind data of the
// code there (a leaf without any is found only by its first byte); null when it is no reflected function's.
const c4_function_info* reflect_function_containing(const void* address);
uint32_t reflect_kind_of(const void* handle);

// --- types ---------------------------------------------------------------------------------------------
const char* reflect_name(const c4_type_info* t);
const char* reflect_type_tag(const c4_type_info* t);              // the source tag it registered under ("" for the program)
uint64_t    reflect_size(const c4_type_info* t);
uint64_t    reflect_align(const c4_type_info* t);
uint32_t    reflect_field_count(const c4_type_info* t);
uint32_t    reflect_method_count(const c4_type_info* t);
uint32_t    reflect_enum_count(const c4_type_info* t);
const char* reflect_base_name(const c4_type_info* t);
int32_t     reflect_is_virtual_type(const c4_type_info* t);
const c4_field_info*  reflect_field(const c4_type_info* t, const char* name);
const c4_method_info* reflect_method(const c4_type_info* t, const char* name);
const c4_field_info*  reflect_field_at(const c4_type_info* t, uint32_t i);
const c4_method_info* reflect_method_at(const c4_type_info* t, uint32_t i);
uint32_t                reflect_type_static_variable_count(const c4_type_info* t);
const c4_variable_info* reflect_type_static_variable_at(const c4_type_info* t, uint32_t i);

// --- members (handle-based, NULL-safe) -------------------------------------------------------------------
const char*  reflect_field_handle_name(const c4_field_info* f);
uint64_t     reflect_field_handle_offset(const c4_field_info* f);
uint64_t     reflect_field_handle_size(const c4_field_info* f);
const char*  reflect_field_handle_type_name(const c4_field_info* f);
uint32_t     reflect_field_handle_is_const(const c4_field_info* f);
uint32_t     reflect_field_handle_is_alias(const c4_field_info* f);
const c4_type_info* reflect_field_type_handle(const c4_field_info* f);

const char*  reflect_method_handle_name(const c4_method_info* m);
uint32_t     reflect_method_handle_param_count(const c4_method_info* m);
uint32_t     reflect_method_handle_is_virtual(const c4_method_info* m);
uint32_t     reflect_method_handle_is_static(const c4_method_info* m);
uint32_t     reflect_method_handle_is_const(const c4_method_info* m);
const char*  reflect_method_handle_return_type_name(const c4_method_info* m);
const c4_param_info* reflect_method_param(const c4_method_info* m, uint32_t i);
const c4_type_info*  reflect_method_return_handle(const c4_method_info* m);

const char*  reflect_param_handle_name(const c4_param_info* p);
const char*  reflect_param_handle_type_name(const c4_param_info* p);
const c4_type_info* reflect_param_type_handle(const c4_param_info* p);

const char*  reflect_variable_handle_name(const c4_variable_info* v);
const char*  reflect_variable_handle_tag(const c4_variable_info* v);
const char*  reflect_variable_handle_type_name(const c4_variable_info* v);
uint32_t     reflect_variable_handle_is_const(const c4_variable_info* v);
const char*  reflect_variable_handle_owner(const c4_variable_info* v);
void*        reflect_variable_address(const c4_variable_info* v);
const c4_type_info* reflect_variable_type_handle(const c4_variable_info* v);

const char*  reflect_function_handle_name(const c4_function_info* f);
const char*  reflect_function_handle_tag(const c4_function_info* f);
const char*  reflect_function_handle_symbol(const c4_function_info* f);
const void*  reflect_function_handle_address(const c4_function_info* f);
uint32_t     reflect_function_handle_param_count(const c4_function_info* f);
uint32_t     reflect_function_handle_is_static(const c4_function_info* f);
const char*  reflect_function_handle_owner(const c4_function_info* f);
const char*  reflect_function_handle_return_type_name(const c4_function_info* f);
const c4_param_info* reflect_function_param(const c4_function_info* f, uint32_t i);
const c4_type_info*  reflect_function_return_handle(const c4_function_info* f);

// --- invoke: 1 when the call happened; 0 for a function with no thunk or a method without an object --------
uint32_t reflect_function_is_invokable(const c4_function_info* f);
uint32_t reflect_method_is_invokable(const c4_method_info* m);
uint32_t reflect_function_invoke(const c4_function_info* f, void* object, void* result, void** arguments);
uint32_t reflect_method_invoke(const c4_method_info* m, void* object, void* result, void** arguments);

// --- enums ---------------------------------------------------------------------------------------------
const c4_enum_value* reflect_enum_at(const c4_type_info* t, uint32_t i);
const char*          reflect_enum_handle_name(const c4_enum_value* e);
int64_t              reflect_enum_handle_value(const c4_enum_value* e);

// --- annotations (D-42): an annotation handle stays valid while its entity is registered ---------------------
uint32_t reflect_type_annotation_count(const c4_type_info* t);
const c4_annotation_info* reflect_type_annotation_at(const c4_type_info* t, uint32_t i);
uint32_t reflect_field_annotation_count(const c4_field_info* f);
const c4_annotation_info* reflect_field_annotation_at(const c4_field_info* f, uint32_t i);
uint32_t reflect_method_annotation_count(const c4_method_info* m);
const c4_annotation_info* reflect_method_annotation_at(const c4_method_info* m, uint32_t i);
uint32_t reflect_param_annotation_count(const c4_param_info* p);
const c4_annotation_info* reflect_param_annotation_at(const c4_param_info* p, uint32_t i);
uint32_t reflect_variable_annotation_count(const c4_variable_info* v);
const c4_annotation_info* reflect_variable_annotation_at(const c4_variable_info* v, uint32_t i);
uint32_t reflect_function_annotation_count(const c4_function_info* f);
const c4_annotation_info* reflect_function_annotation_at(const c4_function_info* f, uint32_t i);
const char* reflect_annotation_name(const c4_annotation_info* a);
uint32_t    reflect_annotation_arg_count(const c4_annotation_info* a);
uint32_t    reflect_annotation_arg_kind(const c4_annotation_info* a, uint32_t i);
int64_t     reflect_annotation_arg_int(const c4_annotation_info* a, uint32_t i);
double      reflect_annotation_arg_float(const c4_annotation_info* a, uint32_t i);
const char* reflect_annotation_arg_string(const c4_annotation_info* a, uint32_t i);

// --- groups: an entity is in a group iff its declaration carried [group("name")] ----------------------------
const void*   reflect_group(const char* name);
uint32_t      reflect_group_handle_count(const void* g);
const void*   reflect_group_handle_item(const void* g, uint32_t i);
uint32_t      reflect_group_name_count(void);
const char*   reflect_group_name_at(uint32_t i);

#endif // C4_REFLECT_ABI_H
