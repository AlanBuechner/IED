// ReflectHandlesC.c - what a reflection handle tells: names, sizes, members, parameters, enum values,
// annotations, and invoking functions and methods. A handle's record is fixed once registered, so reading it
// takes no lock (only a source unload or a re-registration changes it; see ReflectRuntimeC.c).

#include <stdint.h>
#include <string.h>
#include "ReflectABI.h"
#include "Reflect/ReflectRecords.h"

// ------------------------------------------------------------------ types

const char* reflect_name(const c4_type_info* t) { return t ? t->name : 0; }
// A handle is its record's first member (Reflect/ReflectRecords.h): the record holds the tag.
const char* reflect_type_tag(const c4_type_info* t) { return t ? ((const rt_type*)t)->tag : 0; }
uint64_t reflect_size(const c4_type_info* t) { return t ? t->size : 0; }
uint64_t reflect_align(const c4_type_info* t) { return t ? t->align : 0; }
uint32_t reflect_field_count(const c4_type_info* t) { return t ? t->fieldCount : 0; }
uint32_t reflect_method_count(const c4_type_info* t) { return t ? t->methodCount : 0; }
uint32_t reflect_enum_count(const c4_type_info* t) { return t ? t->enumValueCount : 0; }
const char* reflect_base_name(const c4_type_info* t) { return t ? t->baseName : 0; }

// The method array holds rt_method records (the base plus its owned parameters): index by that stride.
static const c4_method_info* methodSlot(const c4_type_info* t, uint32_t i)
{ return (t && i < t->methodCount) ? &((const rt_method*)t->methods)[i].base : 0; }

const c4_field_info* reflect_field_at(const c4_type_info* t, uint32_t i)
{ return (t && i < t->fieldCount) ? &t->fields[i] : 0; }
const c4_method_info* reflect_method_at(const c4_type_info* t, uint32_t i) { return methodSlot(t, i); }

const c4_field_info* reflect_field(const c4_type_info* t, const char* name)
{
    for (uint32_t i = 0; t && name && i < t->fieldCount; ++i)
        if (strcmp(t->fields[i].name, name) == 0) return &t->fields[i];
    return 0;
}

const c4_method_info* reflect_method(const c4_type_info* t, const char* name)
{
    for (uint32_t i = 0; t && name && i < t->methodCount; ++i)
    {
        const c4_method_info* m = methodSlot(t, i);
        if (strcmp(m->name, name) == 0) return m;
    }
    return 0;
}

int32_t reflect_is_virtual_type(const c4_type_info* t)
{
    if (!t || t->kind != reflect_KIND_OBJECT) return 0;
    for (uint32_t i = 0; i < t->methodCount; ++i)
    {
        const c4_method_info* m = methodSlot(t, i);
        if (m->isVirtual && !m->isStatic) return 1;
    }
    return 0;
}

// ------------------------------------------------------------------ members

const char* reflect_field_handle_name(const c4_field_info* f) { return f ? f->name : 0; }
uint64_t reflect_field_handle_offset(const c4_field_info* f) { return f ? f->offset : 0; }
uint64_t reflect_field_handle_size(const c4_field_info* f) { return f ? f->size : 0; }
const char* reflect_field_handle_type_name(const c4_field_info* f) { return f ? f->typeName : 0; }
uint32_t reflect_field_handle_is_const(const c4_field_info* f) { return f ? f->isConst : 0; }
uint32_t reflect_field_handle_is_alias(const c4_field_info* f) { return f ? f->isAlias : 0; }
const c4_type_info* reflect_field_type_handle(const c4_field_info* f) { return f ? reflect_type(f->typeName) : 0; }

const char* reflect_method_handle_name(const c4_method_info* m) { return m ? m->name : 0; }
uint32_t reflect_method_handle_param_count(const c4_method_info* m) { return m ? m->paramCount : 0; }
uint32_t reflect_method_handle_is_virtual(const c4_method_info* m) { return m ? m->isVirtual : 0; }
uint32_t reflect_method_handle_is_static(const c4_method_info* m) { return m ? m->isStatic : 0; }
uint32_t reflect_method_handle_is_const(const c4_method_info* m) { return m ? m->isConst : 0; }
const char* reflect_method_handle_return_type_name(const c4_method_info* m) { return m ? m->returnType : 0; }
const c4_param_info* reflect_method_param(const c4_method_info* m, uint32_t i)
{ return (m && i < m->paramCount) ? &m->params[i] : 0; }
const c4_type_info* reflect_method_return_handle(const c4_method_info* m) { return m ? reflect_type(m->returnType) : 0; }

const char* reflect_param_handle_name(const c4_param_info* p) { return p ? p->name : 0; }
const char* reflect_param_handle_type_name(const c4_param_info* p) { return p ? p->typeName : 0; }
const c4_type_info* reflect_param_type_handle(const c4_param_info* p) { return p ? reflect_type(p->typeName) : 0; }

const char* reflect_variable_handle_name(const c4_variable_info* v) { return v ? v->name : 0; }
const char* reflect_variable_handle_tag(const c4_variable_info* v) { return v ? ((const rt_variable*)v)->tag : 0; }
const char* reflect_variable_handle_type_name(const c4_variable_info* v) { return v ? v->typeName : 0; }
uint32_t reflect_variable_handle_is_const(const c4_variable_info* v) { return v ? v->isConst : 0; }
const char* reflect_variable_handle_owner(const c4_variable_info* v) { return v ? v->ownerType : 0; }
void* reflect_variable_address(const c4_variable_info* v) { return v ? ((const rt_variable*)v)->address : 0; }
const c4_type_info* reflect_variable_type_handle(const c4_variable_info* v) { return v ? reflect_type(v->typeName) : 0; }

const char* reflect_function_handle_name(const c4_function_info* f) { return f ? f->name : 0; }
const char* reflect_function_handle_tag(const c4_function_info* f) { return f ? ((const rt_function*)f)->tag : 0; }
const char* reflect_function_handle_symbol(const c4_function_info* f) { return f ? f->symbol : 0; }
uint32_t reflect_function_handle_param_count(const c4_function_info* f) { return f ? f->paramCount : 0; }
uint32_t reflect_function_handle_is_static(const c4_function_info* f) { return f ? f->isStatic : 0; }
const char* reflect_function_handle_owner(const c4_function_info* f) { return f ? f->ownerType : 0; }
const char* reflect_function_handle_return_type_name(const c4_function_info* f) { return f ? f->returnType : 0; }
const c4_param_info* reflect_function_param(const c4_function_info* f, uint32_t i)
{ return (f && i < f->paramCount) ? &f->params[i] : 0; }
const c4_type_info* reflect_function_return_handle(const c4_function_info* f) { return f ? reflect_type(f->returnType) : 0; }

// ------------------------------------------------------------------ invoke

uint32_t reflect_function_is_invokable(const c4_function_info* f) { return f && f->invoke; }
uint32_t reflect_method_is_invokable(const c4_method_info* m) { return m && m->invoke; }

uint32_t reflect_function_invoke(const c4_function_info* f, void* object, void* result, void** arguments)
{
    if (!reflect_function_is_invokable(f) || (!f->isStatic && !object)) return 0;
    f->invoke(object, result, arguments);
    return 1;
}

uint32_t reflect_method_invoke(const c4_method_info* m, void* object, void* result, void** arguments)
{
    if (!reflect_method_is_invokable(m) || (!m->isStatic && !object)) return 0;
    m->invoke(object, result, arguments);
    return 1;
}

// ------------------------------------------------------------------ enums

const c4_enum_value* reflect_enum_at(const c4_type_info* t, uint32_t i)
{ return (t && i < t->enumValueCount) ? &t->enumValues[i] : 0; }
const char* reflect_enum_handle_name(const c4_enum_value* e) { return e ? e->name : 0; }
int64_t reflect_enum_handle_value(const c4_enum_value* e) { return e ? e->value : 0; }

// ------------------------------------------------------------------ annotations

uint32_t reflect_type_annotation_count(const c4_type_info* t) { return t ? t->annotationCount : 0; }
const c4_annotation_info* reflect_type_annotation_at(const c4_type_info* t, uint32_t i)
{ return (t && i < t->annotationCount) ? &t->annotations[i] : 0; }
uint32_t reflect_field_annotation_count(const c4_field_info* f) { return f ? f->annotationCount : 0; }
const c4_annotation_info* reflect_field_annotation_at(const c4_field_info* f, uint32_t i)
{ return (f && i < f->annotationCount) ? &f->annotations[i] : 0; }
uint32_t reflect_method_annotation_count(const c4_method_info* m) { return m ? m->annotationCount : 0; }
const c4_annotation_info* reflect_method_annotation_at(const c4_method_info* m, uint32_t i)
{ return (m && i < m->annotationCount) ? &m->annotations[i] : 0; }
uint32_t reflect_param_annotation_count(const c4_param_info* p) { return p ? p->annotationCount : 0; }
const c4_annotation_info* reflect_param_annotation_at(const c4_param_info* p, uint32_t i)
{ return (p && i < p->annotationCount) ? &p->annotations[i] : 0; }
uint32_t reflect_variable_annotation_count(const c4_variable_info* v) { return v ? v->annotationCount : 0; }
const c4_annotation_info* reflect_variable_annotation_at(const c4_variable_info* v, uint32_t i)
{ return (v && i < v->annotationCount) ? &v->annotations[i] : 0; }
uint32_t reflect_function_annotation_count(const c4_function_info* f) { return f ? f->annotationCount : 0; }
const c4_annotation_info* reflect_function_annotation_at(const c4_function_info* f, uint32_t i)
{ return (f && i < f->annotationCount) ? &f->annotations[i] : 0; }

static const c4_annotation_arg* annotationArg(const c4_annotation_info* a, uint32_t i)
{ return (a && i < a->argCount) ? &a->args[i] : 0; }

const char* reflect_annotation_name(const c4_annotation_info* a) { return a ? a->name : 0; }
uint32_t reflect_annotation_arg_count(const c4_annotation_info* a) { return a ? a->argCount : 0; }
uint32_t reflect_annotation_arg_kind(const c4_annotation_info* a, uint32_t i)
{ const c4_annotation_arg* arg = annotationArg(a, i); return arg ? arg->kind : 0; }
int64_t reflect_annotation_arg_int(const c4_annotation_info* a, uint32_t i)
{ const c4_annotation_arg* arg = annotationArg(a, i); return arg ? arg->intValue : 0; }
double reflect_annotation_arg_float(const c4_annotation_info* a, uint32_t i)
{ const c4_annotation_arg* arg = annotationArg(a, i); return arg ? arg->floatValue : 0.0; }
const char* reflect_annotation_arg_string(const c4_annotation_info* a, uint32_t i)
{ const c4_annotation_arg* arg = annotationArg(a, i); return arg ? arg->stringValue : 0; }
