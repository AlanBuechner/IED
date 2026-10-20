// Loaded libraries for Bedrock's Platform::Library (PlatformRuntimeC.h) over LoadLibraryExW, GetProcAddress and
// FreeLibrary.

#include "PlatformRuntimeC.h"
#include "PathRuntimeC.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kMessageUnits = 512 };

// `text` into `out`, cut at a whole UTF-8 character when it does not fit; always null-terminated.
static void CopyTruncated(const char* text, char* out, int64_t capacity)
{
	if (!out || capacity <= 0) return;
	int64_t length = (int64_t)strlen(text);
	if (length >= capacity)
	{
		length = capacity - 1;
		while (length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80) --length;
	}
	memcpy(out, text, (size_t)length);
	out[length] = 0;
}

// The system's text for `code` without its line break, then the code: "The specified module could not be found.
// (error 126)".
static void DescribeError(DWORD code, char* out, int64_t capacity)
{
	wchar_t message[kMessageUnits];
	DWORD units = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, message, kMessageUnits, NULL);
	while (units > 0 && (message[units - 1] == L'\r' || message[units - 1] == L'\n' || message[units - 1] == L' ')) --units;
	message[units] = 0;
	char text[kMessageUnits * 3 + 32];
	const int64_t length = c4_wide_to_utf8(message, text, sizeof(text));
	snprintf(text + (length > 0 ? length : 0), 32, "%s(error %lu)", length > 0 ? " " : "", (unsigned long)code);
	CopyTruncated(text, out, capacity);
}

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
	CopyTruncated("", error, errorCapacity);
	if (!path || !*path)
	{
		CopyTruncated("no library path was given", error, errorCapacity);
		return NULL;
	}
	SetLastError(ERROR_SUCCESS);
	HMODULE module = Load(path);
	if (!module) DescribeError(GetLastError() ? GetLastError() : ERROR_INVALID_NAME, error, errorCapacity);
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
