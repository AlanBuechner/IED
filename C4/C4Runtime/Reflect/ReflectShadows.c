#include "Reflect/ReflectShadows.h"
#include <stdlib.h>
#include <string.h>

// Each kind keeps the same three operations; only the record type and how it rejoins its groups differ. A
// registration behind the record is in no group (groups list the record), so releasing it detaches nothing.

void rt_shadow_type(rt_type* t, rt_groups* groups)
{
    rt_type* saved = (rt_type*)malloc(sizeof(rt_type));
    if (!saved) { rt_type* older = t->shadow; rt_release_type(t, groups); t->shadow = older; return; }
    rt_groups_detach(groups, &t->base);
    *saved = *t;
    memset(t, 0, sizeof(*t));
    t->shadow = saved;
}

void rt_drop_type_shadows(rt_type* t, rt_groups* groups, const char* tag)
{
    for (rt_type** link = &t->shadow; *link;)
    {
        rt_type* behind = *link;
        if (strcmp(behind->tag, tag) != 0) { link = &behind->shadow; continue; }
        *link = behind->shadow;
        rt_release_type(behind, groups);
        free(behind);
    }
}

int rt_restore_type(rt_type* t, rt_groups* groups)
{
    rt_type* saved = t->shadow;
    rt_release_type(t, groups);
    if (!saved) return 0;
    *t = *saved;
    free(saved);
    rt_groups_attach(groups, t->base.kind, &t->base, t->base.groupCount, t->base.groups);
    return 1;
}

void rt_shadow_variable(rt_variable* v, rt_groups* groups)
{
    rt_variable* saved = (rt_variable*)malloc(sizeof(rt_variable));
    if (!saved) { rt_variable* older = v->shadow; rt_release_variable(v, groups); v->shadow = older; return; }
    rt_groups_detach(groups, &v->base);
    *saved = *v;
    memset(v, 0, sizeof(*v));
    v->shadow = saved;
}

void rt_drop_variable_shadows(rt_variable* v, rt_groups* groups, const char* tag)
{
    for (rt_variable** link = &v->shadow; *link;)
    {
        rt_variable* behind = *link;
        if (strcmp(behind->tag, tag) != 0) { link = &behind->shadow; continue; }
        *link = behind->shadow;
        rt_release_variable(behind, groups);
        free(behind);
    }
}

int rt_restore_variable(rt_variable* v, rt_groups* groups)
{
    rt_variable* saved = v->shadow;
    rt_release_variable(v, groups);
    if (!saved) return 0;
    *v = *saved;
    free(saved);
    rt_groups_attach(groups, reflect_KIND_VARIABLE, &v->base, v->base.groupCount, v->base.groups);
    return 1;
}

void rt_shadow_function(rt_function* f, rt_groups* groups)
{
    rt_function* saved = (rt_function*)malloc(sizeof(rt_function));
    if (!saved) { rt_function* older = f->shadow; rt_release_function(f, groups); f->shadow = older; return; }
    rt_groups_detach(groups, &f->base);
    *saved = *f;
    memset(f, 0, sizeof(*f));
    f->shadow = saved;
}

void rt_drop_function_shadows(rt_function* f, rt_groups* groups, const char* tag)
{
    for (rt_function** link = &f->shadow; *link;)
    {
        rt_function* behind = *link;
        if (strcmp(behind->tag, tag) != 0) { link = &behind->shadow; continue; }
        *link = behind->shadow;
        rt_release_function(behind, groups);
        free(behind);
    }
}

int rt_restore_function(rt_function* f, rt_groups* groups)
{
    rt_function* saved = f->shadow;
    rt_release_function(f, groups);
    if (!saved) return 0;
    *f = *saved;
    free(saved);
    rt_groups_attach(groups, reflect_KIND_FUNCTION, &f->base, f->base.groupCount, f->base.groups);
    return 1;
}
