#include "Reflect/ReflectCodeIndex.h"
#include <stdlib.h>

static int by_start(const void* a, const void* b)
{
    const uintptr_t x = ((const rt_code_entry*)a)->start;
    const uintptr_t y = ((const rt_code_entry*)b)->start;
    return (x > y) - (x < y);
}

int rt_code_index_build(rt_code_index* index, const c4_function_info* const* functions, uint32_t count)
{
    if (count > index->capacity)
    {
        rt_code_entry* grown = (rt_code_entry*)realloc(index->entries, (size_t)count * sizeof(rt_code_entry));
        if (!grown) return 0;
        index->entries = grown;
        index->capacity = count;
    }
    index->count = 0;
    for (uint32_t i = 0; i < count; i++)
        if (functions[i]->address) index->entries[index->count++] = (rt_code_entry){ (uintptr_t)functions[i]->address, functions[i] };
    qsort(index->entries, index->count, sizeof(rt_code_entry), by_start);
    return 1;
}

const c4_function_info* rt_code_index_find(const rt_code_index* index, const void* start)
{
    const uintptr_t key = (uintptr_t)start;
    uint32_t low = 0;
    uint32_t high = index->count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (index->entries[middle].start < key) low = middle + 1;
        else high = middle;
    }
    return low < index->count && index->entries[low].start == key ? index->entries[low].function : 0;
}
