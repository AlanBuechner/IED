// Source lines of code addresses for Bedrock's Platform::SourceLineOf (PlatformRuntimeC.h), from the PDBs of the
// process's modules through DbgHelp. DbgHelp is loaded on the first question, not linked, so a program that never
// asks never loads it. Its calls are not thread-safe and go one at a time. The session is the process's handle
// duplicated, never GetCurrentProcess() itself, so it is not one another part of the process opened.

#include "PlatformRuntimeC.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <string.h>

typedef DWORD(WINAPI* SetOptionsFn)(DWORD);
typedef BOOL(WINAPI* InitializeFn)(HANDLE, PCWSTR, BOOL);
typedef BOOL(WINAPI* LineFromAddressFn)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINEW64);
typedef BOOL(WINAPI* RefreshModulesFn)(HANDLE);

// The process's one DbgHelp session for source lines.
typedef struct SourceLines
{
	SRWLOCK lock;
	int opened;                         // an attempt was made: a failed one is not made again
	HANDLE session;                     // null when DbgHelp did not load or start
	LineFromAddressFn lineFromAddress;
	RefreshModulesFn refreshModules;
} SourceLines;

static SourceLines g_lines = { SRWLOCK_INIT, 0, NULL, NULL, NULL };

static HANDLE StartSession(HMODULE dbghelp)
{
	const SetOptionsFn setOptions = (SetOptionsFn)GetProcAddress(dbghelp, "SymSetOptions");
	const InitializeFn initialize = (InitializeFn)GetProcAddress(dbghelp, "SymInitializeW");
	HANDLE session = NULL;
	if (!setOptions || !initialize ||
		!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &session, 0, FALSE, DUPLICATE_SAME_ACCESS))
		return NULL;
	setOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
	if (initialize(session, NULL, TRUE)) return session;
	CloseHandle(session);
	return NULL;
}

static void Open(SourceLines* lines)
{
	lines->opened = 1;
	HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!dbghelp) return;
	lines->lineFromAddress = (LineFromAddressFn)GetProcAddress(dbghelp, "SymGetLineFromAddrW64");
	lines->refreshModules = (RefreshModulesFn)GetProcAddress(dbghelp, "SymRefreshModuleList");
	if (lines->lineFromAddress) lines->session = StartSession(dbghelp);
}

// The line of `code`, looking again at the process's modules once when none has it (a debug script's DLL loaded
// since the session started).
static BOOL Find(SourceLines* lines, const void* code, IMAGEHLP_LINEW64* found)
{
	DWORD displacement = 0;
	memset(found, 0, sizeof(*found));
	found->SizeOfStruct = sizeof(*found);
	if (lines->lineFromAddress(lines->session, (DWORD64)(uintptr_t)code, &displacement, found)) return TRUE;
	return lines->refreshModules && lines->refreshModules(lines->session) &&
		lines->lineFromAddress(lines->session, (DWORD64)(uintptr_t)code, &displacement, found);
}

int32_t c4_source_line_of(const void* code, char* file, int32_t capacity, int32_t* line)
{
	int32_t length = 0;
	*line = 0;
	if (!code || !file || capacity <= 0) return 0;
	AcquireSRWLockExclusive(&g_lines.lock);
	if (!g_lines.opened) Open(&g_lines);
	IMAGEHLP_LINEW64 found;
	if (g_lines.session && Find(&g_lines, code, &found))
	{
		const int written = WideCharToMultiByte(CP_UTF8, 0, found.FileName, -1, file, capacity, NULL, NULL);
		if (written > 0)
		{
			length = written - 1;
			*line = (int32_t)found.LineNumber;
		}
	}
	ReleaseSRWLockExclusive(&g_lines.lock);
	return length;
}
