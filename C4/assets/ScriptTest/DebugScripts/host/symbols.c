#include "symbols.h"
#include <windows.h>
#include <dbghelp.h>
#include <string.h>

#pragma comment(lib, "dbghelp.lib")

static HMODULE ModuleOf(const void* address)
{
	HMODULE module = NULL;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)address, &module);
	return module;
}

int32_t ModuleFileOf(const void* address, char* out, int32_t capacity)
{
	char path[MAX_PATH];
	HMODULE module = ModuleOf(address);
	if (!module || !GetModuleFileNameA(module, path, MAX_PATH)) return 0;
	const char* name = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;
	const int32_t length = (int32_t)strlen(name);
	if (length >= capacity) return 0;
	memcpy(out, name, (size_t)length + 1);
	return length;
}

int32_t ModulePathOf(const void* address, char* out, int32_t capacity)
{
	HMODULE module = ModuleOf(address);
	if (!module) return 0;
	const DWORD length = GetModuleFileNameA(module, out, (DWORD)capacity);
	return length > 0 && length < (DWORD)capacity ? (int32_t)length : 0;
}

int32_t SourceLineOf(const void* address, char* file, int32_t capacity)
{
	HANDLE session = (HANDLE)(uintptr_t)0x5C41D0; // not the process: the host's own DbgHelp use is untouched
	char path[MAX_PATH];
	HMODULE module = ModuleOf(address);
	if (!module || !GetModuleFileNameA(module, path, MAX_PATH)) return 0;
	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
	if (!SymInitialize(session, NULL, FALSE)) return 0;
	int32_t line = 0;
	if (SymLoadModuleEx(session, NULL, path, NULL, (DWORD64)(uintptr_t)module, 0, NULL, 0))
	{
		IMAGEHLP_LINE64 found;
		DWORD displacement = 0;
		memset(&found, 0, sizeof(found));
		found.SizeOfStruct = sizeof(found);
		if (SymGetLineFromAddr64(session, (DWORD64)(uintptr_t)address, &displacement, &found) && (int32_t)strlen(found.FileName) < capacity)
		{
			memcpy(file, found.FileName, strlen(found.FileName) + 1);
			line = (int32_t)found.LineNumber;
		}
	}
	SymCleanup(session);
	return line;
}

int32_t ModuleLoaded(const char* name) { return GetModuleHandleA(name) != NULL; }
