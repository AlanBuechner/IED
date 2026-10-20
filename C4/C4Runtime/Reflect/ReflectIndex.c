#include "Reflect/ReflectIndex.h"
#include <stdlib.h>
#include <string.h>

enum { kFirstTableCapacity = 64, kFirstIndexCapacity = 64 };

static uint32_t hash_text(const char* s)
{
    uint32_t h = 2166136261u;
    for (; s && *s; ++s) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

int rt_table_add(rt_table* t, void* item)
{
    if (t->count == t->capacity)
    {
        uint32_t capacity = t->capacity ? t->capacity * 2 : kFirstTableCapacity;
        void** grown = (void**)realloc(t->items, capacity * sizeof(void*));
        if (!grown) return 0;
        t->items = grown;
        t->capacity = capacity;
    }
    t->items[t->count++] = item;
    return 1;
}

int rt_table_holds(const rt_table* t, const void* item)
{
    for (uint32_t i = 0; i < t->count; ++i) if (t->items[i] == item) return 1;
    return 0;
}

static void index_place(rt_index* x, const rt_table* t, uint32_t position)
{
    const char* key = x->key(t->items[position]);
    uint32_t mask = x->capacity - 1;
    for (uint32_t i = hash_text(key) & mask;; i = (i + 1) & mask)
    {
        if (!x->slots[i]) { x->slots[i] = position + 1; return; }
        if (strcmp(x->key(t->items[x->slots[i] - 1]), key) == 0) return; // the first one keeps the key
    }
}

// Every position re-placed into a table at most half full.
void rt_index_rebuild(rt_index* x, const rt_table* t)
{
    uint32_t capacity = kFirstIndexCapacity;
    while (capacity < t->count * 2 + 2) capacity *= 2;
    uint32_t* slots = (uint32_t*)calloc(capacity, sizeof(uint32_t));
    if (!slots) return;
    free(x->slots);
    x->slots = slots;
    x->capacity = capacity;
    for (uint32_t p = 0; p < t->count; ++p) index_place(x, t, p);
}

void rt_index_add(rt_index* x, const rt_table* t, uint32_t position)
{
    if (!x->slots || (t->count * 2 + 2) > x->capacity) { rt_index_rebuild(x, t); return; }
    index_place(x, t, position);
}

void* rt_index_find(const rt_index* x, const rt_table* t, const char* key)
{
    if (!x->slots || !key) return 0;
    uint32_t mask = x->capacity - 1;
    for (uint32_t i = hash_text(key) & mask; x->slots[i]; i = (i + 1) & mask)
    {
        void* item = t->items[x->slots[i] - 1];
        if (strcmp(x->key(item), key) == 0) return item;
    }
    return 0;
}
