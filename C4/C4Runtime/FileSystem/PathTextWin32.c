// UTF-8 text as the UTF-16 the Windows API takes, and back (PathRuntimeC.h).

#include "PathRuntimeC.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>

wchar_t* c4_utf8_to_wide(const char* text)
{
	if (!text) return NULL;
	int units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
	if (units <= 0) return NULL;
	wchar_t* wide = (wchar_t*)malloc((size_t)units * sizeof(wchar_t));
	if (wide && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, units) != units)
	{
		free(wide);
		return NULL;
	}
	return wide;
}

int64_t c4_wide_to_utf8(const wchar_t* wide, char* out, int64_t cap)
{
	int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
	if (bytes > 0 && out && cap >= bytes) WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, bytes, NULL, NULL);
	return bytes > 0 ? (int64_t)bytes - 1 : -1;
}
