#include "ReflectABI.h"
#include "script.h"

static int g_values[4] = { 0, 10, 20, 30 };
static const c4_annotation_arg kArgs[4][1] = { { { reflect_ARG_INT, 0, 0.0, "" } }, { { reflect_ARG_INT, 1, 0.0, "" } },
    { { reflect_ARG_INT, 2, 0.0, "" } }, { { reflect_ARG_INT, 3, 0.0, "" } } };
static const c4_annotation_info kAnnotations[4][1] = { { { "Version", 1, kArgs[0] } }, { { "Version", 1, kArgs[1] } },
    { { "Version", 1, kArgs[2] } }, { { "Version", 1, kArgs[3] } } };

// Each version's start writes its number, and its flags differ: what a registration in front says is what lookups see.
static void ConstructOne(void* object) { *(int*)object = 1; }
static void ConstructThree(void* object) { *(int*)object = 3; }
static const c4_construct_fn kConstructs[4] = { 0, ConstructOne, 0, ConstructThree };
static const uint32_t kFlags[4] = { 0, reflect_TYPE_DEFAULT_CONSTRUCTIBLE | reflect_TYPE_COPYABLE, reflect_TYPE_HOLDS_POINTERS,
    reflect_TYPE_DEFAULT_CONSTRUCTIBLE };

unsigned int ScriptRegisterUnder(const char* tag, int version)
{
    const c4_type_info type = { reflect_KIND_OBJECT, "Script::Enemy", 4, 4, 0, 0, 0, 0, "", 0, 0, 0, 0, 1, kAnnotations[version],
        kConstructs[version], 0, 0, kFlags[version] };
    const c4_variable_info variable = { "Script::g_value", "script_g_value", "int32", 4, 4, 0, "", 0, 0, 1, kAnnotations[version] };
    const c4_function_info function = { "Script::Tick", "script_tick", "void", 0, 0, 1, 0, "", 0, 0, 0, 1, kAnnotations[version] };
    void* addresses[1] = { &g_values[version] };
    const c4_reflect_image image = { 1, &type, 1, &variable, addresses, 1, &function };
    c4_reflect_register_image(tag, &image);
    return 3;
}

int* ScriptValueOf(int version) { return &g_values[version]; }

void ShadowHostType(const char* tag)
{
    const c4_type_info type = { reflect_KIND_OBJECT, "Host::Thing", 16, 8, 0, 0, 0, 0, "", 0, 0, 0, 0, 0, 0 };
    c4_reflect_register_type(tag, &type);
}
