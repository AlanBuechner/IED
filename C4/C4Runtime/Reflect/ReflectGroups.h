#pragma once
// Reflection groups: a group is a list of (kind, handle); an entity is in it iff its declaration carried
// [group("name")]. Each entry is allocated on its own, so a group handle stays valid while groups are added.

#include <stdint.h>

typedef struct rt_group_item { uint32_t kind; const void* handle; } rt_group_item;
typedef struct rt_group { char* name; rt_group_item* items; uint32_t count; uint32_t capacity; } rt_group;
typedef struct rt_groups { rt_group** entries; uint32_t count; uint32_t capacity; } rt_groups;

rt_group* rt_groups_find(const rt_groups* groups, const char* name);
// Adds `handle` to each group `names` lists, making the groups that do not exist yet.
void rt_groups_attach(rt_groups* groups, uint32_t kind, const void* handle, uint32_t count, const char* const* names);
// Removes `handle` from every group.
void rt_groups_detach(rt_groups* groups, const void* handle);
