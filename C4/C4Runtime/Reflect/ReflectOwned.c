#include "Reflect/ReflectOwned.h"
#include <stdlib.h>
#include <string.h>

char* rt_dup_str(const char* s)
{
    size_t n = (s ? strlen(s) : 0) + 1;
    char* p = (char*)malloc(n);
    if (!p) return 0;
    if (s) memcpy(p, s, n - 1);
    p[n - 1] = 0;
    return p;
}

const char** rt_dup_strings(uint32_t count, const char* const* strings)
{
    if (!count || !strings) return 0;
    const char** copy = (const char**)calloc(count, sizeof(char*));
    if (!copy) return 0;
    for (uint32_t i = 0; i < count; ++i) copy[i] = rt_dup_str(strings[i]);
    return copy;
}

void rt_free_strings(uint32_t count, const char** strings)
{
    for (uint32_t i = 0; strings && i < count; ++i) free((char*)strings[i]);
    free(strings);
}

void rt_own_annotations(uint32_t* count, const c4_annotation_info** annotations)
{
    const c4_annotation_info* source = *annotations;
    c4_annotation_info* copy = (*count && source) ? (c4_annotation_info*)calloc(*count, sizeof(c4_annotation_info)) : 0;
    for (uint32_t i = 0; copy && i < *count; ++i)
    {
        copy[i].name = rt_dup_str(source[i].name);
        c4_annotation_arg* args = source[i].argCount ? (c4_annotation_arg*)calloc(source[i].argCount, sizeof(c4_annotation_arg)) : 0;
        for (uint32_t a = 0; args && a < source[i].argCount; ++a)
        {
            args[a] = source[i].args[a];
            args[a].stringValue = rt_dup_str(source[i].args[a].stringValue);
        }
        copy[i].argCount = args ? source[i].argCount : 0;
        copy[i].args = args;
    }
    if (!copy) *count = 0;
    *annotations = copy;
}

void rt_release_annotations(uint32_t count, const c4_annotation_info* annotations)
{
    for (uint32_t i = 0; annotations && i < count; ++i)
    {
        free((char*)annotations[i].name);
        for (uint32_t a = 0; a < annotations[i].argCount; ++a) free((char*)annotations[i].args[a].stringValue);
        free((void*)annotations[i].args);
    }
    free((void*)annotations);
}

c4_param_info* rt_dup_params(uint32_t count, const c4_param_info* params)
{
    if (!count || !params) return 0;
    c4_param_info* copy = (c4_param_info*)calloc(count, sizeof(c4_param_info));
    if (!copy) return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        copy[i] = params[i];
        copy[i].name = rt_dup_str(params[i].name);
        copy[i].typeName = rt_dup_str(params[i].typeName);
        rt_own_annotations(&copy[i].annotationCount, &copy[i].annotations);
    }
    return copy;
}

void rt_free_params(uint32_t count, c4_param_info* params)
{
    for (uint32_t i = 0; params && i < count; ++i)
    {
        free((char*)params[i].name);
        free((char*)params[i].typeName);
        rt_release_annotations(params[i].annotationCount, params[i].annotations);
    }
    free(params);
}
