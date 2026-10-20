#pragma once
// What the Win32 process files share: the command line and environment a child starts with (CommandLineWin32.c) and
// the pipes to it (PipesWin32.c).

#include "ProcessRuntimeC.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>

// The program's full path (malloc'd): a path as it is, a bare name looked for as the system does. Null with `error`
// when there is none, or it is a batch file (cmd.exe reads arguments its own way: run it through "cmd.exe /c").
wchar_t* c4_process_win32_program(const char* program, char* error, int64_t errorCapacity);
// The program and the arguments, quoted for CommandLineToArgvW (malloc'd); null when out of memory or not UTF-8.
wchar_t* c4_process_win32_command_line(const c4_process_options* start, const wchar_t* program);
// The child's environment block (malloc'd), or null to inherit this one unchanged; `*failed` for an entry that is
// not UTF-8 or names nothing.
wchar_t* c4_process_win32_environment(const c4_process_options* start, bool* failed);

// A pipe whose parent end (`*parent`, overlapped) reads when `parentReads`, else writes; `*child` is the other end,
// inheritable.
bool c4_process_win32_pipe(bool parentReads, HANDLE* parent, HANDLE* child);
void c4_process_win32_close(HANDLE* handle);
