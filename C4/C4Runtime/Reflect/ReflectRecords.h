#pragma once
// The registry's records: a registered type, variable or function with every table and string it points at
// owned by the record, so a handle stays valid when its identity registers again (it is refilled in place).

#include "ReflectABI.h"
#include "Reflect/ReflectGroups.h"

typedef struct rt_method {
    c4_method_info base;
    c4_param_info* params;
} rt_method;

typedef struct rt_type {
    c4_type_info base;                // fields/methods/enumValues/groups point at the owned arrays below
    char* tag;
    c4_field_info* fields;
    rt_method* methods;
    c4_enum_value* enumValues;
    const char** groups;
    struct rt_type* shadow;           // the registration this one hides (Reflect/ReflectShadows.h), or null
} rt_type;

typedef struct rt_variable {
    c4_variable_info base;
    char* tag;
    void* address;                    // the variable's live storage
    const char** groups;
    struct rt_variable* shadow;
} rt_variable;

typedef struct rt_function {
    c4_function_info base;
    char* tag;
    c4_param_info* params;
    const char** groups;
    struct rt_function* shadow;
} rt_function;

// Fill copies `info` into the record and adds it to its groups (it leaves `shadow` as it is); release frees what
// the record owns, takes it out of its groups and zeroes it, `shadow` too.
void rt_fill_type(rt_type* t, rt_groups* groups, const char* tag, const c4_type_info* info);
void rt_release_type(rt_type* t, rt_groups* groups);
void rt_fill_variable(rt_variable* v, rt_groups* groups, const char* tag, const c4_variable_info* info, void* address);
void rt_release_variable(rt_variable* v, rt_groups* groups);
void rt_fill_function(rt_function* f, rt_groups* groups, const char* tag, const c4_function_info* info);
void rt_release_function(rt_function* f, rt_groups* groups);
