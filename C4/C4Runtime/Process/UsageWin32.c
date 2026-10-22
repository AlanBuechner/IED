// What a process uses, for Bedrock's Process::GetUsage and Child::GetUsage (ProcessRuntimeC.h): its processor time so
// far and its memory now. By id it asks only for limited query rights, which a program started by another user may
// still refuse, and answers only while the process runs; through a handle it answers after the end too, with totals.

#include "ProcessRuntimeC.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <string.h>

static int64_t FileTimeNs(FILETIME time)
{
	ULARGE_INTEGER ticks;
	ticks.LowPart = time.dwLowDateTime;
	ticks.HighPart = time.dwHighDateTime;
	return (int64_t)ticks.QuadPart * 100;
}

// An ended process stays openable by id while any handle anywhere keeps it; it is gone all the same.
static BOOL Running(HANDLE process)
{
	DWORD exitCode = 0;
	return GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
}

int32_t c4_process_handle_usage(void* process, c4_process_counters* usage)
{
	if (!usage) return -1;
	memset(usage, 0, sizeof(*usage));
	if (!process) return -1;
	FILETIME created, exited, kernel, user;
	PROCESS_MEMORY_COUNTERS_EX counters;
	if (!GetProcessTimes((HANDLE)process, &created, &exited, &kernel, &user)) return -1;
	if (!GetProcessMemoryInfo((HANDLE)process, (PROCESS_MEMORY_COUNTERS*)&counters, sizeof(counters))) return -1;
	usage->cpuTimeNs = FileTimeNs(kernel) + FileTimeNs(user);
	usage->workingSetBytes = (int64_t)counters.WorkingSetSize;
	usage->privateBytes = (int64_t)counters.PrivateUsage;
	usage->peakPrivateBytes = (int64_t)counters.PeakPagefileUsage;
	usage->peakWorkingSetBytes = (int64_t)counters.PeakWorkingSetSize;
	return 0;
}

int32_t c4_process_usage(uint32_t id, c4_process_counters* usage)
{
	if (!usage) return -1;
	memset(usage, 0, sizeof(*usage));
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
	if (!process) return -1;
	const int32_t result = Running(process) ? c4_process_handle_usage(process, usage) : -1;
	CloseHandle(process);
	return result;
}
