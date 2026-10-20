#include "Reflect/ReflectRecords.h"
#include "Reflect/ReflectOwned.h"
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------ types

static c4_field_info* own_fields(uint32_t count, const c4_field_info* source)
{
    if (!count || !source) return 0;
    c4_field_info* fields = (c4_field_info*)calloc(count, sizeof(c4_field_info));
    for (uint32_t i = 0; fields && i < count; ++i)
    {
        fields[i] = source[i];
        fields[i].name = rt_dup_str(source[i].name);
        fields[i].typeName = rt_dup_str(source[i].typeName);
        rt_own_annotations(&fields[i].annotationCount, &fields[i].annotations);
    }
    return fields;
}

static rt_method* own_methods(uint32_t count, const c4_method_info* source)
{
    if (!count || !source) return 0;
    rt_method* methods = (rt_method*)calloc(count, sizeof(rt_method));
    for (uint32_t i = 0; methods && i < count; ++i)
    {
        rt_method* m = &methods[i];
        m->base = source[i];
        m->base.name = rt_dup_str(source[i].name);
        m->base.returnType = rt_dup_str(source[i].returnType);
        m->params = rt_dup_params(source[i].paramCount, source[i].params);
        m->base.params = m->params;
        if (!m->params) m->base.paramCount = 0;
        rt_own_annotations(&m->base.annotationCount, &m->base.annotations);
    }
    return methods;
}

static c4_enum_value* own_enum_values(uint32_t count, const c4_enum_value* source)
{
    if (!count || !source) return 0;
    c4_enum_value* values = (c4_enum_value*)calloc(count, sizeof(c4_enum_value));
    for (uint32_t i = 0; values && i < count; ++i)
    {
        values[i].name = rt_dup_str(source[i].name);
        values[i].value = source[i].value;
    }
    return values;
}

void rt_fill_type(rt_type* t, rt_groups* groups, const char* tag, const c4_type_info* info)
{
    t->base = *info;
    t->tag = rt_dup_str(tag);
    t->base.name = rt_dup_str(info->name);
    t->base.baseName = rt_dup_str(info->baseName);
    t->fields = own_fields(info->fieldCount, info->fields);
    t->methods = own_methods(info->methodCount, info->methods);
    t->enumValues = own_enum_values(info->enumValueCount, info->enumValues);
    t->groups = rt_dup_strings(info->groupCount, info->groups);
    rt_own_annotations(&t->base.annotationCount, &t->base.annotations);
    t->base.fields = t->fields;
    t->base.methods = t->methods ? &t->methods[0].base : 0; // rt_method strides; methodSlot() indexes by it
    t->base.enumValues = t->enumValues;
    if (!t->fields) t->base.fieldCount = 0; // a copy that ran out of memory has nothing to show
    if (!t->methods) t->base.methodCount = 0;
    if (!t->enumValues) t->base.enumValueCount = 0;
    if (!t->groups) t->base.groupCount = 0;
    t->base.groups = t->groups;
    rt_groups_attach(groups, info->kind, &t->base, info->groupCount, info->groups);
}

void rt_release_type(rt_type* t, rt_groups* groups)
{
    rt_groups_detach(groups, &t->base);
    free((char*)t->base.name);
    free((char*)t->base.baseName);
    for (uint32_t i = 0; t->fields && i < t->base.fieldCount; ++i)
    {
        free((char*)t->fields[i].name);
        free((char*)t->fields[i].typeName);
        rt_release_annotations(t->fields[i].annotationCount, t->fields[i].annotations);
    }
    free(t->fields);
    for (uint32_t i = 0; t->methods && i < t->base.methodCount; ++i)
    {
        free((char*)t->methods[i].base.name);
        free((char*)t->methods[i].base.returnType);
        rt_free_params(t->methods[i].base.paramCount, t->methods[i].params);
        rt_release_annotations(t->methods[i].base.annotationCount, t->methods[i].base.annotations);
    }
    free(t->methods);
    for (uint32_t i = 0; t->enumValues && i < t->base.enumValueCount; ++i) free((char*)t->enumValues[i].name);
    free(t->enumValues);
    rt_free_strings(t->base.groupCount, t->groups);
    rt_release_annotations(t->base.annotationCount, t->base.annotations);
    free(t->tag);
    memset(t, 0, sizeof(*t));
}

// ------------------------------------------------------------------ variables

void rt_fill_variable(rt_variable* v, rt_groups* groups, const char* tag, const c4_variable_info* info, void* address)
{
    v->base = *info;
    v->tag = rt_dup_str(tag);
    v->base.name = rt_dup_str(info->name);
    v->base.symbol = rt_dup_str(info->symbol);
    v->base.typeName = rt_dup_str(info->typeName);
    v->base.ownerType = rt_dup_str(info->ownerType);
    v->groups = rt_dup_strings(info->groupCount, info->groups);
    v->base.groups = v->groups;
    if (!v->groups) v->base.groupCount = 0;
    rt_own_annotations(&v->base.annotationCount, &v->base.annotations);
    v->address = address;
    rt_groups_attach(groups, reflect_KIND_VARIABLE, &v->base, info->groupCount, info->groups);
}

void rt_release_variable(rt_variable* v, rt_groups* groups)
{
    rt_groups_detach(groups, &v->base);
    free((char*)v->base.name); free((char*)v->base.symbol); free((char*)v->base.typeName); free((char*)v->base.ownerType);
    rt_free_strings(v->base.groupCount, v->groups);
    rt_release_annotations(v->base.annotationCount, v->base.annotations);
    free(v->tag);
    memset(v, 0, sizeof(*v));
}

// ------------------------------------------------------------------ functions

void rt_fill_function(rt_function* f, rt_groups* groups, const char* tag, const c4_function_info* info)
{
    f->base = *info;
    f->tag = rt_dup_str(tag);
    f->base.name = rt_dup_str(info->name);
    f->base.symbol = rt_dup_str(info->symbol);
    f->base.returnType = rt_dup_str(info->returnType);
    f->base.ownerType = rt_dup_str(info->ownerType);
    f->params = rt_dup_params(info->paramCount, info->params);
    f->base.params = f->params;
    f->groups = rt_dup_strings(info->groupCount, info->groups);
    f->base.groups = f->groups;
    if (!f->params) f->base.paramCount = 0;
    if (!f->groups) f->base.groupCount = 0;
    rt_own_annotations(&f->base.annotationCount, &f->base.annotations);
    rt_groups_attach(groups, reflect_KIND_FUNCTION, &f->base, info->groupCount, info->groups);
}

void rt_release_function(rt_function* f, rt_groups* groups)
{
    rt_groups_detach(groups, &f->base);
    free((char*)f->base.name); free((char*)f->base.symbol); free((char*)f->base.returnType); free((char*)f->base.ownerType);
    rt_free_params(f->base.paramCount, f->params);
    rt_free_strings(f->base.groupCount, f->groups);
    rt_release_annotations(f->base.annotationCount, f->base.annotations);
    free(f->tag);
    memset(f, 0, sizeof(*f));
}
