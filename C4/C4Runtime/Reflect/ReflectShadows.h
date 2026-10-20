#pragma once
// Registrations of one identity under several tags (docs/runtime/04 section 5). The latest is the record every
// handle points at; the earlier ones wait behind it, owned, so unloading the tag that holds the record brings the
// one before back. Two scripts that make the same template instance, or a script that registers what its host
// did, keep each other's.

#include "Reflect/ReflectRecords.h"

// Moves the record's registration behind it, leaving the record empty for the new one.
void rt_shadow_type(rt_type* t, rt_groups* groups);
void rt_shadow_variable(rt_variable* v, rt_groups* groups);
void rt_shadow_function(rt_function* f, rt_groups* groups);

// Frees the registrations of `tag` waiting behind the record.
void rt_drop_type_shadows(rt_type* t, rt_groups* groups, const char* tag);
void rt_drop_variable_shadows(rt_variable* v, rt_groups* groups, const char* tag);
void rt_drop_function_shadows(rt_function* f, rt_groups* groups, const char* tag);

// Releases the record's registration and brings back the latest one behind it. 0 when there is none: the record is
// then released and empty.
int rt_restore_type(rt_type* t, rt_groups* groups);
int rt_restore_variable(rt_variable* v, rt_groups* groups);
int rt_restore_function(rt_function* f, rt_groups* groups);
