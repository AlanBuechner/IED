// What a running process uses, by its id, for Bedrock's Process::GetUsage (ProcessRuntimeC.h): its processor time so
// far and its memory now. Asks only for limited query rights, which a program started by another user may still refuse.

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

int32_t c4_process_usage(uint32_t id, c4_process_counters* usage)
{
	if (!usage) return -1;
	memset(usage, 0, sizeof(*usage));
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
	if (!process) return -1;
	if (!Running(process))
	{
		CloseHandle(process);
		return -1;
	}
	FILETIME created, exited, kernel, user;
	PROCESS_MEMORY_COUNTERS_EX counters;
	const BOOL timed = GetProcessTimes(process, &created, &exited, &kernel, &user);
	const BOOL measured = GetProcessMemoryInfo(process, (PROCESS_MEMORY_COUNTERS*)&counters, sizeof(counters));
	CloseHandle(process);
	if (!timed || !measured) return -1;
	usage->cpuTimeNs = FileTimeNs(kernel) + FileTimeNs(user);
	usage->workingSetBytes = (int64_t)counters.WorkingSetSize;
	usage->privateBytes = (int64_t)counters.PrivateUsage;
	usage->peakPrivateBytes = (int64_t)counters.PeakPagefileUsage;
	return 0;
}
