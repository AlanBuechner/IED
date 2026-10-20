#pragma once
// Owned deep copies of the strings and tables a registration passes in, and their release.

#include <stdint.h>
#include "ReflectABI.h"

char* rt_dup_str(const char* s);
const char** rt_dup_strings(uint32_t count, const char* const* strings);
void rt_free_strings(uint32_t count, const char** strings);

// Replaces the annotation table `*annotations` points at with an owned deep copy (none when out of memory).
void rt_own_annotations(uint32_t* count, const c4_annotation_info** annotations);
void rt_release_annotations(uint32_t count, const c4_annotation_info* annotations);

c4_param_info* rt_dup_params(uint32_t count, const c4_param_info* params);
void rt_free_params(uint32_t count, c4_param_info* params);
