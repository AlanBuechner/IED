#include "Reflect/ReflectGroups.h"
#include "Reflect/ReflectOwned.h"
#include <stdlib.h>
#include <string.h>

enum { kFirstGroupCapacity = 16, kFirstItemCapacity = 8 };

rt_group* rt_groups_find(const rt_groups* groups, const char* name)
{
    for (uint32_t i = 0; name && i < groups->count; ++i)
        if (strcmp(groups->entries[i]->name, name) == 0) return groups->entries[i];
    return 0;
}

static rt_group* group_named(rt_groups* groups, const char* name)
{
    rt_group* g = rt_groups_find(groups, name);
    if (g) return g;
    if (groups->count == groups->capacity)
    {
        uint32_t capacity = groups->capacity ? groups->capacity * 2 : kFirstGroupCapacity;
        rt_group** grown = (rt_group**)realloc(groups->entries, capacity * sizeof(rt_group*));
        if (!grown) return 0;
        groups->entries = grown;
        groups->capacity = capacity;
    }
    g = (rt_group*)calloc(1, sizeof(*g));
    if (!g) return 0;
    g->name = rt_dup_str(name);
    groups->entries[groups->count++] = g;
    return g;
}

static void group_add(rt_group* g, uint32_t kind, const void* handle)
{
    if (g->count == g->capacity)
    {
        uint32_t capacity = g->capacity ? g->capacity * 2 : kFirstItemCapacity;
        rt_group_item* grown = (rt_group_item*)realloc(g->items, capacity * sizeof(rt_group_item));
        if (!grown) return;
        g->items = grown;
        g->capacity = capacity;
    }
    g->items[g->count].kind = kind;
    g->items[g->count].handle = handle;
    g->count++;
}

void rt_groups_attach(rt_groups* groups, uint32_t kind, const void* handle, uint32_t count, const char* const* names)
{
    for (uint32_t i = 0; names && i < count; ++i)
    {
        if (!names[i] || !names[i][0]) continue;
        rt_group* g = group_named(groups, names[i]);
        if (g) group_add(g, kind, handle);
    }
}

void rt_groups_detach(rt_groups* groups, const void* handle)
{
    for (uint32_t gi = 0; gi < groups->count; ++gi)
    {
        rt_group* g = groups->entries[gi];
        for (uint32_t i = 0; i < g->count; )
        {
            if (g->items[i].handle == handle) g->items[i] = g->items[--g->count];
            else ++i;
        }
    }
}
