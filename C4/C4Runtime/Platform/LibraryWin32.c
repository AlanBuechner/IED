// Loaded libraries for Bedrock's Platform::Library (PlatformRuntimeC.h) over LoadLibraryExW, GetProcAddress and
// FreeLibrary.

#include "PlatformRuntimeC.h"
#include "PathRuntimeC.h"
#include "SystemErrorC.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool HasFolder(const char* path) { return strchr(path, '/') || strchr(path, '\\'); }

// A path with a folder is made absolute, so its own folder can be searched first for what the library needs.
static wchar_t* FullPath(const wchar_t* path)
{
	const DWORD units = GetFullPathNameW(path, 0, NULL, NULL);
	wchar_t* full = units ? (wchar_t*)malloc(units * sizeof(wchar_t)) : NULL;
	if (full && GetFullPathNameW(path, units, full, NULL) >= units)
	{
		free(full);
		full = NULL;
	}
	return full;
}

static HMODULE Load(const char* path)
{
	wchar_t* wide = c4_utf8_to_wide(path);
	if (!wide) return NULL;
	wchar_t* full = HasFolder(path) ? FullPath(wide) : NULL;
	DWORD previousMode = 0;
	SetThreadErrorMode(SEM_FAILCRITICALERRORS, &previousMode); // no dialog for a missing dependency
	HMODULE module = full ? LoadLibraryExW(full, NULL, LOAD_WITH_ALTERED_SEARCH_PATH) : LoadLibraryExW(wide, NULL, 0);
	const DWORD error = GetLastError();
	SetThreadErrorMode(previousMode, NULL);
	SetLastError(error);
	free(full);
	free(wide);
	return module;
}

void* c4_library_open(const char* path, char* error, int64_t errorCapacity)
{
	c4_copy_text("", error, errorCapacity);
	if (!path || !*path)
	{
		c4_copy_text("no library path was given", error, errorCapacity);
		return NULL;
	}
	SetLastError(ERROR_SUCCESS);
	HMODULE module = Load(path);
	if (!module) c4_system_error_text(GetLastError() ? GetLastError() : ERROR_INVALID_NAME, error, errorCapacity);
	return module;
}

void* c4_library_find(void* library, const char* symbol)
{
	if (!library || !symbol || !*symbol) return NULL;
	return (void*)GetProcAddress((HMODULE)library, symbol);
}

void c4_library_close(void* library)
{
	if (library) FreeLibrary((HMODULE)library);
}
