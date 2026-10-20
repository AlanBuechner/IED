#pragma once
// Growable tables of records and the hash indexes that find a record by a key it carries.

#include <stdint.h>

typedef struct rt_table { void** items; uint32_t count; uint32_t capacity; } rt_table;
typedef const char* (*rt_key)(const void* item);
// Open addressing over item positions: a slot holds position + 1, 0 is empty. The first item with a key wins.
typedef struct rt_index { uint32_t* slots; uint32_t capacity; rt_key key; } rt_index;

// 0 when out of memory.
int rt_table_add(rt_table* t, void* item);
int rt_table_holds(const rt_table* t, const void* item);

void rt_index_rebuild(rt_index* x, const rt_table* t);
// Indexes the item at `position` (the table's last one).
void rt_index_add(rt_index* x, const rt_table* t, uint32_t position);
void* rt_index_find(const rt_index* x, const rt_table* t, const char* key);
