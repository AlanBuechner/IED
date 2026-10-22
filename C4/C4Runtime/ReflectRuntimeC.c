// ReflectRuntimeC.c - C4 reflection runtime: the registry behind the `reflect_*` builtins (ABI: ReflectABI.h).
//
// The compiler emits a program's tables as one image, registered by a process-startup constructor (a script's by
// its loader) under one hold of the lock. The registry copies every table and string into records it owns
// (Reflect/ReflectRecords), so unloading a source tag frees exactly that tag's records. Lookups go through hash
// indexes. An identity another tag registered keeps that registration behind the new one (Reflect/ReflectShadows), so
// unloading a tag brings back what it hid. A reader-writer lock makes the registry safe to use from any thread: lookups share it, registration and
// unloading take it alone. Unload listeners run without it. Reading a handle needs no lock (ReflectHandlesC.c).

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include "ReflectABI.h"
#include "Reflect/ReflectIndex.h"
#include "Reflect/ReflectOwned.h"
#include "Reflect/ReflectRecords.h"
#include "Reflect/ReflectShadows.h"

static const char* type_name(const void* item) { return ((const rt_type*)item)->base.name; }
static const char* variable_symbol(const void* item) { return ((const rt_variable*)item)->base.symbol; }
static const char* variable_name(const void* item) { return ((const rt_variable*)item)->base.name; }
static const char* function_symbol(const void* item) { return ((const rt_function*)item)->base.symbol; }
static const char* function_name(const void* item) { return ((const rt_function*)item)->base.name; }

typedef struct { uint32_t id; char* tag; c4_unload_fn fn; void* user; } rt_listener;

typedef struct rt_registry {
    SRWLOCK lock;
    rt_table types, vars, fns;
    rt_index typeByName, varBySymbol, varByName, fnBySymbol, fnByName;
    rt_groups groups;
    rt_listener* listeners;
    uint32_t listenerCount, listenerCapacity, nextListenerId;
} rt_registry;

// The C ABI carries no registry parameter: this is the process's one registry.
static rt_registry g_registry = {
    SRWLOCK_INIT, { 0 }, { 0 }, { 0 },
    { 0, 0, type_name }, { 0, 0, variable_symbol }, { 0, 0, variable_name }, { 0, 0, function_symbol }, { 0, 0, function_name },
    { 0 }, 0, 0, 0, 1 };

static rt_registry* reading(void) { AcquireSRWLockShared(&g_registry.lock); return &g_registry; }
static void done_reading(rt_registry* r) { ReleaseSRWLockShared(&r->lock); }
static rt_registry* writing(void) { AcquireSRWLockExclusive(&g_registry.lock); return &g_registry; }
static void done_writing(rt_registry* r) { ReleaseSRWLockExclusive(&r->lock); }

// ------------------------------------------------------------------ registration

static const c4_type_info* register_type(rt_registry* r, const char* tag, const c4_type_info* info)
{
    rt_type* t = (rt_type*)rt_index_find(&r->typeByName, &r->types, info->name);
    if (t)
    {
        if (strcmp(t->tag, tag) != 0) rt_shadow_type(t, &r->groups);
        else { rt_type* behind = t->shadow; rt_release_type(t, &r->groups); t->shadow = behind; }
        rt_fill_type(t, &r->groups, tag, info);
        return &t->base;
    }
    t = (rt_type*)calloc(1, sizeof(rt_type));
    if (!t) return 0;
    rt_fill_type(t, &r->groups, tag, info);
    if (!rt_table_add(&r->types, t)) { rt_release_type(t, &r->groups); free(t); return 0; }
    rt_index_add(&r->typeByName, &r->types, r->types.count - 1);
    return &t->base;
}

const c4_type_info* c4_reflect_register_type(const char* tag, const c4_type_info* info)
{
    if (!info || !info->name || !info->name[0]) return 0;
    rt_registry* r = writing();
    const c4_type_info* registered = register_type(r, tag ? tag : "", info);
    done_writing(r);
    return registered;
}

// Whether a record registered again under its symbol takes another name: only then does the name index change, as the
// record keeps its place in the table (rebuilding it for every function a script registers again was quadratic).
static int renamed(const char* before, const char* after)
{
    return before != after && (!before || !after || strcmp(before, after) != 0);
}

static const c4_variable_info* register_variable(rt_registry* r, const char* tag, const c4_variable_info* info, void* address)
{
    rt_variable* v = (rt_variable*)rt_index_find(&r->varBySymbol, &r->vars, info->symbol);
    if (v)
    {
        const int rename = renamed(v->base.name, info->name);
        if (strcmp(v->tag, tag) != 0) rt_shadow_variable(v, &r->groups);
        else { rt_variable* behind = v->shadow; rt_release_variable(v, &r->groups); v->shadow = behind; }
        rt_fill_variable(v, &r->groups, tag, info, address);
        if (rename) rt_index_rebuild(&r->varByName, &r->vars);
        return &v->base;
    }
    v = (rt_variable*)calloc(1, sizeof(rt_variable));
    if (!v) return 0;
    rt_fill_variable(v, &r->groups, tag, info, address);
    if (!rt_table_add(&r->vars, v)) { rt_release_variable(v, &r->groups); free(v); return 0; }
    rt_index_add(&r->varBySymbol, &r->vars, r->vars.count - 1);
    rt_index_add(&r->varByName, &r->vars, r->vars.count - 1);
    return &v->base;
}

const c4_variable_info* c4_reflect_register_variable(const char* tag, const c4_variable_info* info, void* address)
{
    if (!info || !info->symbol || !info->symbol[0]) return 0;
    rt_registry* r = writing();
    const c4_variable_info* registered = register_variable(r, tag ? tag : "", info, address);
    done_writing(r);
    return registered;
}

static const c4_function_info* register_function(rt_registry* r, const char* tag, const c4_function_info* info)
{
    rt_function* f = (rt_function*)rt_index_find(&r->fnBySymbol, &r->fns, info->symbol);
    if (f)
    {
        const int rename = renamed(f->base.name, info->name);
        if (strcmp(f->tag, tag) != 0) rt_shadow_function(f, &r->groups);
        else { rt_function* behind = f->shadow; rt_release_function(f, &r->groups); f->shadow = behind; }
        rt_fill_function(f, &r->groups, tag, info);
        if (rename) rt_index_rebuild(&r->fnByName, &r->fns);
        return &f->base;
    }
    f = (rt_function*)calloc(1, sizeof(rt_function));
    if (!f) return 0;
    rt_fill_function(f, &r->groups, tag, info);
    if (!rt_table_add(&r->fns, f)) { rt_release_function(f, &r->groups); free(f); return 0; }
    rt_index_add(&r->fnBySymbol, &r->fns, r->fns.count - 1);
    rt_index_add(&r->fnByName, &r->fns, r->fns.count - 1);
    return &f->base;
}

const c4_function_info* c4_reflect_register_function(const char* tag, const c4_function_info* info)
{
    if (!info || !info->symbol || !info->symbol[0]) return 0;
    rt_registry* r = writing();
    const c4_function_info* registered = register_function(r, tag ? tag : "", info);
    done_writing(r);
    return registered;
}

// The first byte of the function whose code holds `address`, by its unwind data: a chained entry (code the compiler
// moved apart) leads to its function's primary one. NULL when the code there has none.
static const void* code_start(const void* address)
{
    DWORD64 imageBase = 0;
    PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry((DWORD64)(uintptr_t)address, &imageBase, NULL);
    for (int hops = 0; entry && hops < 32; ++hops)
    {
        if (entry->UnwindData & 1u) { entry = (PRUNTIME_FUNCTION)(imageBase + (entry->UnwindData & ~(DWORD)1u)); continue; }
        const uint8_t* info = (const uint8_t*)(imageBase + entry->UnwindData);
        const uint8_t kChainInfo = 4;
        if (((info[0] >> 3) & kChainInfo) == 0) return (const void*)(imageBase + entry->BeginAddress);
        const uint8_t codeCount = info[2];
        entry = (PRUNTIME_FUNCTION)(info + 4 + 2u * ((codeCount + 1u) & ~1u));
    }
    return NULL;
}

const c4_function_info* reflect_function_containing(const void* address)
{
    if (!address) return 0;
    const void* found_start = code_start(address);
    const void* start = found_start ? found_start : address;   // a leaf without unwind data: only its first byte
    rt_registry* r = reading();
    const c4_function_info* found = 0;
    for (uint32_t i = 0; i < r->fns.count && !found; i++)
    {
        const rt_function* f = (const rt_function*)r->fns.items[i];
        if (f->base.address == start) found = &f->base;
    }
    done_reading(r);
    return found;
}

void c4_reflect_register_image(const char* tag, const c4_reflect_image* image)
{
    if (!image) return;
    if (!tag) tag = "";
    rt_registry* r = writing();
    for (uint32_t i = 0; i < image->typeCount; i++)
        if (image->types[i].name && image->types[i].name[0]) register_type(r, tag, &image->types[i]);
    for (uint32_t i = 0; i < image->variableCount; i++)
        if (image->variables[i].symbol && image->variables[i].symbol[0]) register_variable(r, tag, &image->variables[i], image->addresses[i]);
    for (uint32_t i = 0; i < image->functionCount; i++)
        if (image->functions[i].symbol && image->functions[i].symbol[0]) register_function(r, tag, &image->functions[i]);
    done_writing(r);
}

// ------------------------------------------------------------------ unload listeners

uint32_t c4_reflect_on_unload(const char* tag, c4_unload_fn fn, void* user)
{
    if (!fn) return 0;
    rt_registry* r = writing();
    uint32_t id = 0;
    if (r->listenerCount == r->listenerCapacity)
    {
        uint32_t capacity = r->listenerCapacity ? r->listenerCapacity * 2 : 8;
        rt_listener* grown = (rt_listener*)realloc(r->listeners, capacity * sizeof(rt_listener));
        if (grown) { r->listeners = grown; r->listenerCapacity = capacity; }
    }
    if (r->listenerCount < r->listenerCapacity)
    {
        rt_listener* l = &r->listeners[r->listenerCount++];
        id = l->id = r->nextListenerId++;
        l->tag = rt_dup_str(tag);
        l->fn = fn;
        l->user = user;
    }
    done_writing(r);
    return id;
}

void c4_reflect_remove_listener(uint32_t id)
{
    rt_registry* r = writing();
    for (uint32_t i = 0; i < r->listenerCount; ++i)
    {
        if (r->listeners[i].id != id) continue;
        free(r->listeners[i].tag);
        r->listeners[i] = r->listeners[--r->listenerCount];
        break;
    }
    done_writing(r);
}

// The listeners for `tag`, copied with their tags so a listener may add or remove listeners while they run.
static rt_listener* listeners_of(const char* tag, uint32_t* count)
{
    rt_registry* r = reading();
    rt_listener* snapshot = r->listenerCount ? (rt_listener*)malloc(r->listenerCount * sizeof(rt_listener)) : 0;
    *count = 0;
    for (uint32_t i = 0; snapshot && i < r->listenerCount; ++i)
        if (!r->listeners[i].tag[0] || strcmp(r->listeners[i].tag, tag) == 0) snapshot[(*count)++] = r->listeners[i];
    done_reading(r);
    return snapshot;
}

static void notify_unload(const char* tag)
{
    uint32_t count = 0;
    rt_listener* snapshot = listeners_of(tag, &count);
    for (uint32_t i = 0; i < count; ++i) snapshot[i].fn(tag, snapshot[i].user);
    free(snapshot);
}

// ------------------------------------------------------------------ unload (scripting hot reload)

// What unloading needs of one kind of record.
typedef struct rt_kind_ops {
    const char* (*tag)(const void* record);
    void (*drop_shadows)(void* record, rt_groups* groups, const char* tag);
    int (*restore)(void* record, rt_groups* groups);
} rt_kind_ops;

static const char* type_tag(const void* t) { return ((const rt_type*)t)->tag; }
static const char* variable_tag(const void* v) { return ((const rt_variable*)v)->tag; }
static const char* function_tag(const void* f) { return ((const rt_function*)f)->tag; }
static void drop_type_shadows(void* t, rt_groups* g, const char* tag) { rt_drop_type_shadows((rt_type*)t, g, tag); }
static void drop_variable_shadows(void* v, rt_groups* g, const char* tag) { rt_drop_variable_shadows((rt_variable*)v, g, tag); }
static void drop_function_shadows(void* f, rt_groups* g, const char* tag) { rt_drop_function_shadows((rt_function*)f, g, tag); }
static int restore_type(void* t, rt_groups* g) { return rt_restore_type((rt_type*)t, g); }
static int restore_variable(void* v, rt_groups* g) { return rt_restore_variable((rt_variable*)v, g); }
static int restore_function(void* f, rt_groups* g) { return rt_restore_function((rt_function*)f, g); }
static const rt_kind_ops kTypeOps = { type_tag, drop_type_shadows, restore_type };
static const rt_kind_ops kVariableOps = { variable_tag, drop_variable_shadows, restore_variable };
static const rt_kind_ops kFunctionOps = { function_tag, drop_function_shadows, restore_function };

// Takes `tag`'s registrations out of `t`: the ones waiting behind records go; a record `tag` holds gets back the
// registration it hid, or is freed when it hid none. The others keep their order. Returns how many records `tag`
// held.
static uint32_t remove_tagged(rt_table* t, rt_groups* groups, const char* tag, const rt_kind_ops* ops)
{
    uint32_t kept = 0, removed = 0;
    for (uint32_t i = 0; i < t->count; ++i)
    {
        void* record = t->items[i];
        ops->drop_shadows(record, groups, tag);
        const int held = strcmp(ops->tag(record), tag) == 0;
        removed += held;
        if (held && !ops->restore(record, groups)) free(record);
        else t->items[kept++] = record;
    }
    t->count = kept;
    return removed;
}

static uint32_t remove_source(rt_registry* r, const char* tag)
{
    uint32_t removed = remove_tagged(&r->types, &r->groups, tag, &kTypeOps)
        + remove_tagged(&r->vars, &r->groups, tag, &kVariableOps)
        + remove_tagged(&r->fns, &r->groups, tag, &kFunctionOps);
    rt_index_rebuild(&r->typeByName, &r->types);
    rt_index_rebuild(&r->varBySymbol, &r->vars);
    rt_index_rebuild(&r->varByName, &r->vars);
    rt_index_rebuild(&r->fnBySymbol, &r->fns);
    rt_index_rebuild(&r->fnByName, &r->fns);
    return removed;
}

void c4_reflect_notify_unload(const char* tag)
{
    if (tag && tag[0]) notify_unload(tag);
}

uint32_t c4_reflect_remove_source(const char* tag)
{
    if (!tag || !tag[0]) return 0;
    rt_registry* r = writing();
    const uint32_t removed = remove_source(r, tag);
    done_writing(r);
    return removed;
}

uint32_t c4_reflect_unload_source(const char* tag)
{
    c4_reflect_notify_unload(tag);
    return c4_reflect_remove_source(tag);
}

// ------------------------------------------------------------------ lookups

const c4_type_info* reflect_type(const char* name)
{
    rt_registry* r = reading();
    rt_type* t = (rt_type*)rt_index_find(&r->typeByName, &r->types, name);
    done_reading(r);
    return t ? &t->base : 0;
}

const c4_variable_info* reflect_variable(const char* name)
{
    rt_registry* r = reading();
    rt_variable* v = (rt_variable*)rt_index_find(&r->varByName, &r->vars, name);
    done_reading(r);
    return v ? &v->base : 0;
}

const c4_function_info* reflect_function(const char* name)
{
    rt_registry* r = reading();
    rt_function* f = (rt_function*)rt_index_find(&r->fnByName, &r->fns, name);
    done_reading(r);
    return f ? &f->base : 0;
}

// A free function `name` registered under `tag` (a name may also be a method's, or another tag's).
static int is_free_function_of(const rt_function* f, const char* tag, const char* name)
{
    return f->tag && f->base.name && f->base.ownerType && f->base.ownerType[0] == 0 && strcmp(f->tag, tag) == 0 &&
        strcmp(f->base.name, name) == 0;
}

const c4_function_info* reflect_function_in(const char* tag, const char* name)
{
    rt_registry* r = reading();
    const c4_function_info* found = 0;
    for (uint32_t i = 0; i < r->fns.count && !found; ++i)
    {
        const rt_function* f = (const rt_function*)r->fns.items[i];
        if (is_free_function_of(f, tag, name)) found = &f->base;
    }
    done_reading(r);
    return found;
}

const char* reflect_function_symbol_in(const char* tag, const char* name)
{
    rt_registry* r = reading();
    const char* symbol = 0;
    for (uint32_t i = 0; i < r->fns.count && !symbol; ++i)
        for (const rt_function* f = (const rt_function*)r->fns.items[i]; f && !symbol; f = f->shadow)
            if (is_free_function_of(f, tag, name)) symbol = f->base.symbol;
    done_reading(r);
    return symbol;
}

static uint32_t count_of(const rt_table* t)
{
    rt_registry* r = reading();
    const uint32_t count = t->count;
    done_reading(r);
    return count;
}

uint32_t reflect_count(void) { return count_of(&g_registry.types); }
uint32_t reflect_variable_count(void) { return count_of(&g_registry.vars); }
uint32_t reflect_function_count(void) { return count_of(&g_registry.fns); }

static void* item_at(const rt_table* t, uint32_t i)
{
    rt_registry* r = reading();
    void* item = i < t->count ? t->items[i] : 0;
    done_reading(r);
    return item;
}

// Each record starts with its handle.
const c4_type_info* reflect_type_by_index(uint32_t i) { return (const c4_type_info*)item_at(&g_registry.types, i); }
const c4_variable_info* reflect_variable_by_index(uint32_t i) { return (const c4_variable_info*)item_at(&g_registry.vars, i); }
const c4_function_info* reflect_function_by_index(uint32_t i) { return (const c4_function_info*)item_at(&g_registry.fns, i); }

uint32_t reflect_kind_of(const void* handle)
{
    if (!handle) return 0;
    rt_registry* r = reading();
    uint32_t kind = 0;
    if (rt_table_holds(&r->types, handle)) kind = ((const c4_type_info*)handle)->kind;
    else if (rt_table_holds(&r->vars, handle)) kind = reflect_KIND_VARIABLE;
    else if (rt_table_holds(&r->fns, handle)) kind = reflect_KIND_FUNCTION;
    done_reading(r);
    return kind;
}

// A type's static data members: the registered variables it owns, in registration order.
uint32_t reflect_type_static_variable_count(const c4_type_info* t)
{
    if (!t) return 0;
    rt_registry* r = reading();
    uint32_t n = 0;
    for (uint32_t i = 0; i < r->vars.count; ++i)
        if (strcmp(((rt_variable*)r->vars.items[i])->base.ownerType, t->name) == 0) n++;
    done_reading(r);
    return n;
}

const c4_variable_info* reflect_type_static_variable_at(const c4_type_info* t, uint32_t index)
{
    if (!t) return 0;
    rt_registry* r = reading();
    const c4_variable_info* found = 0;
    for (uint32_t i = 0; !found && i < r->vars.count; ++i)
    {
        rt_variable* v = (rt_variable*)r->vars.items[i];
        if (strcmp(v->base.ownerType, t->name) == 0 && index-- == 0) found = &v->base;
    }
    done_reading(r);
    return found;
}

// ------------------------------------------------------------------ groups

const void* reflect_group(const char* name)
{
    rt_registry* r = reading();
    const rt_group* g = rt_groups_find(&r->groups, name);
    done_reading(r);
    return g;
}

uint32_t reflect_group_handle_count(const void* group)
{
    if (!group) return 0;
    rt_registry* r = reading();
    const uint32_t count = ((const rt_group*)group)->count;
    done_reading(r);
    return count;
}

const void* reflect_group_handle_item(const void* group, uint32_t i)
{
    if (!group) return 0;
    rt_registry* r = reading();
    const rt_group* g = (const rt_group*)group;
    const void* handle = i < g->count ? g->items[i].handle : 0;
    done_reading(r);
    return handle;
}

uint32_t reflect_group_name_count(void)
{
    rt_registry* r = reading();
    const uint32_t count = r->groups.count;
    done_reading(r);
    return count;
}

const char* reflect_group_name_at(uint32_t i)
{
    rt_registry* r = reading();
    const char* name = i < r->groups.count ? r->groups.entries[i]->name : 0;
    done_reading(r);
    return name;
}
