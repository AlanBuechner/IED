// Loaded libraries for Bedrock's Platform::Library (PlatformRuntimeC.h) over dlopen, dlsym and dlclose. A library's
// symbols are all bound when it loads (RTLD_NOW), so one it needs and cannot find fails the open as on Windows, and it
// keeps its symbols to itself (RTLD_LOCAL), as a DLL does. Unlike Windows, the libraries it needs are found as the
// system finds them (its RUNPATH, LD_LIBRARY_PATH, the system's folders), beside it only when its RUNPATH names
// $ORIGIN, and a bare name is not looked for beside the program. The error is the loader's text (dlerror), which
// names the file and the reason; it carries no error code.

#include "PlatformRuntimeC.h"
#include "SystemErrorC.h"
#include <dlfcn.h>
#include <stddef.h>

void* c4_library_open(const char* path, char* error, int64_t errorCapacity)
{
	c4_copy_text("", error, errorCapacity);
	if (!path || !*path)
	{
		c4_copy_text("no library path was given", error, errorCapacity);
		return NULL;
	}
	void* library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!library)
	{
		const char* why = dlerror();
		c4_copy_text(why ? why : "the library did not load", error, errorCapacity);
	}
	return library;
}

void* c4_library_find(void* library, const char* symbol)
{
	if (!library || !symbol || !*symbol) return NULL;
	return dlsym(library, symbol);
}

void c4_library_close(void* library)
{
	if (library) dlclose(library);
}
