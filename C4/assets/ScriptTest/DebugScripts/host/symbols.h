// What a debugger sees of an address (DbgHelp, on a session of its own): the module that holds it and the source line.
#pragma once
#include <stdint.h>

// The file name (no folder) of the module holding `address` into `out`: its length, 0 when no module holds it.
int32_t ModuleFileOf(const void* address, char* out, int32_t capacity);
// The full path of the module holding `address` into `out`: its length, 0 when no module holds it.
int32_t ModulePathOf(const void* address, char* out, int32_t capacity);
// The source file and line of `address` from its module's debug information: the line, 0 when there is none.
int32_t SourceLineOf(const void* address, char* file, int32_t capacity);
// Whether a module of that file name (no folder) is loaded.
int32_t ModuleLoaded(const char* name);
