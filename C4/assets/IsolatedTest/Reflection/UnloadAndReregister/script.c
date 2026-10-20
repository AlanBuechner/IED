#include "ReflectABI.h"
#include "script.h"

static int g_scriptValue = 5;
static const c4_annotation_arg kVersionArgs[] = { { reflect_ARG_INT, 3, 0.0, "" } };
static const c4_annotation_info kAnnotations[] = { { "Version", 1, kVersionArgs } };
static const c4_field_info kFields[] = { { "hp", "int32", 4, 0, 0, 0, 0, 0 } };
static const c4_type_info kType = { reflect_KIND_OBJECT, "Script::Enemy", 4, 4, 1, kFields, 0, 0, "", 0, 0, 0, 0, 1, kAnnotations };
static const c4_variable_info kVariable = { "Script::g_value", "script_g_value", "int32", 4, 4, 0, "", 0, 0, 0, 0 };
static const c4_function_info kFunction = { "Script::Tick", "script_tick", "void", 0, 0, 1, 0, "", 0, 0, 0, 0, 0 };

unsigned int ScriptRegister(void)
{
    unsigned int count = 0;
    count += c4_reflect_register_type("script", &kType) != 0;
    count += c4_reflect_register_variable("script", &kVariable, &g_scriptValue) != 0;
    count += c4_reflect_register_function("script", &kFunction) != 0;
    return count;
}
