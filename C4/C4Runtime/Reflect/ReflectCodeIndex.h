#pragma once
// Reflected functions by where their code starts (reflect_function_containing): sorted once, searched by halves.

#include <stdint.h>
#include "ReflectABI.h"

typedef struct rt_code_entry { uintptr_t start; const c4_function_info* function; } rt_code_entry;
typedef struct rt_code_index { rt_code_entry* entries; uint32_t count; uint32_t capacity; } rt_code_index;

// The index of every function of `functions` (`count` records) that has code, sorted. 0 when out of memory.
int rt_code_index_build(rt_code_index* index, const c4_function_info* const* functions, uint32_t count);
// The function whose code starts at `start`, or null.
const c4_function_info* rt_code_index_find(const rt_code_index* index, const void* start);
