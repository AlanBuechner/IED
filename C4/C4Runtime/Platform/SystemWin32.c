// System and process information for Bedrock's Platform namespace (PlatformRuntimeC.h): processors and caches,
// memory, the executable's path and environment variables.

#include "PlatformRuntimeC.h"
#include "PathRuntimeC.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <bcrypt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum { kDefaultCacheLineBytes = 64 };
// GetModuleFileNameW has no way to ask for the length; a path is at most this many UTF-16 units.
enum { kFirstPathUnits = MAX_PATH, kMaxPathUnits = 32768 };

static void CountCache(const CACHE_RELATIONSHIP* cache, c4_processor_info* info)
{
	const bool holdsData = cache->Type == CacheData || cache->Type == CacheUnified;
	if (!holdsData) return;
	if (cache->Level == 1)
	{
		info->l1DataBytes = (int64_t)cache->CacheSize;
		info->cacheLineBytes = (int32_t)cache->LineSize;
	}
	else if (cache->Level == 2) info->l2Bytes = (int64_t)cache->CacheSize;
	else if (cache->Level == 3) info->l3Bytes = (int64_t)cache->CacheSize;
}

static void CountRelation(const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* relation, c4_processor_info* info)
{
	switch (relation->Relationship)
	{
	case RelationProcessorCore: info->cores++; break;
	case RelationProcessorPackage: info->packages++; break;
	case RelationCache: CountCache(&relation->Cache, info); break;
	default: break;
	}
}

static void CountTopology(c4_processor_info* info)
{
	DWORD bytes = 0;
	GetLogicalProcessorInformationEx(RelationAll, NULL, &bytes);
	char* buffer = bytes ? (char*)malloc(bytes) : NULL;
	if (buffer && GetLogicalProcessorInformationEx(RelationAll, (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)buffer, &bytes))
	{
		for (DWORD offset = 0; offset < bytes;)
		{
			const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* relation = (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(buffer + offset);
			CountRelation(relation, info);
			offset += relation->Size;
		}
	}
	free(buffer);
}

void c4_system_processor_info(c4_processor_info* info)
{
	if (!info) return;
	memset(info, 0, sizeof(*info));
	CountTopology(info);
	const DWORD logical = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
	info->logicalProcessors = logical ? (int32_t)logical : 1;
	if (info->cores == 0) info->cores = info->logicalProcessors;
	if (info->packages == 0) info->packages = 1;
	if (info->cacheLineBytes == 0) info->cacheLineBytes = kDefaultCacheLineBytes;
}

void c4_system_memory_status(c4_memory_status* status)
{
	if (!status) return;
	memset(status, 0, sizeof(*status));
	MEMORYSTATUSEX memory;
	memory.dwLength = sizeof(memory);
	if (!GlobalMemoryStatusEx(&memory)) return;
	status->totalPhysicalBytes = (int64_t)memory.ullTotalPhys;
	status->availablePhysicalBytes = (int64_t)memory.ullAvailPhys;
	status->commitLimitBytes = (int64_t)memory.ullTotalPageFile;
	status->availableCommitBytes = (int64_t)memory.ullAvailPageFile;
	status->totalVirtualBytes = (int64_t)memory.ullTotalVirtual;
	status->availableVirtualBytes = (int64_t)memory.ullAvailVirtual;
}

void c4_system_process_memory(c4_process_memory* memory)
{
	if (!memory) return;
	memset(memory, 0, sizeof(*memory));
	PROCESS_MEMORY_COUNTERS_EX counters;
	if (!GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&counters, sizeof(counters))) return;
	memory->workingSetBytes = (int64_t)counters.WorkingSetSize;
	memory->peakWorkingSetBytes = (int64_t)counters.PeakWorkingSetSize;
	memory->privateBytes = (int64_t)counters.PrivateUsage;
	memory->peakPrivateBytes = (int64_t)counters.PeakPagefileUsage;
}

uint64_t c4_system_process_id(void) { return GetCurrentProcessId(); }

static DWORD NextPathUnits(DWORD units) { return units * 2 < kMaxPathUnits ? units * 2 : kMaxPathUnits; }

int64_t c4_system_executable_path(char* out, int64_t capacity)
{
	for (DWORD units = kFirstPathUnits;; units = NextPathUnits(units))
	{
		wchar_t* wide = (wchar_t*)malloc(units * sizeof(wchar_t));
		if (!wide) return -1;
		const DWORD written = GetModuleFileNameW(NULL, wide, units);
		const int64_t length = written > 0 && written < units ? c4_wide_to_utf8(wide, out, capacity) : -1;
		free(wide);
		if (written < units || units == kMaxPathUnits) return length;
	}
}

// A variable's name as the wide API takes it; null when it is empty, has an '=' or is not UTF-8.
static wchar_t* VariableName(const char* name)
{
	if (!name || !*name || strchr(name, '=')) return NULL;
	return c4_utf8_to_wide(name);
}

// The value of `name` as a malloc'd wide string ("" when set to nothing), or null when it is not set.
static wchar_t* VariableValue(const wchar_t* name)
{
	for (;;)
	{
		SetLastError(ERROR_SUCCESS);
		const DWORD units = GetEnvironmentVariableW(name, NULL, 0);
		if (units == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return NULL;
		const DWORD room = units ? units : 1;
		wchar_t* value = (wchar_t*)calloc(room, sizeof(wchar_t));
		if (!value) return NULL;
		const DWORD written = GetEnvironmentVariableW(name, value, room);
		if (written < room) return value;
		free(value); // it grew in between: ask again
	}
}

int64_t c4_system_get_environment(const char* name, char* out, int64_t capacity)
{
	wchar_t* wideName = VariableName(name);
	wchar_t* value = wideName ? VariableValue(wideName) : NULL;
	const int64_t length = value ? c4_wide_to_utf8(value, out, capacity) : -1;
	free(value);
	free(wideName);
	return length;
}

int32_t c4_system_set_environment(const char* name, const char* value)
{
	wchar_t* wideName = VariableName(name);
	wchar_t* wideValue = value ? c4_utf8_to_wide(value) : NULL;
	const bool valid = wideName && (wideValue || !value);
	const bool set = valid && (SetEnvironmentVariableW(wideName, wideValue) || GetLastError() == ERROR_ENVVAR_NOT_FOUND);
	free(wideValue);
	free(wideName);
	return set ? 0 : -1;
}

#pragma comment(lib, "bcrypt.lib")

// BCryptGenRandom takes a ULONG count: larger requests go in pieces of this size.
enum { kRandomChunkBytes = 1 << 30 };

int32_t c4_system_random_bytes(void* out, int64_t count)
{
	if (count < 0 || (count > 0 && !out)) return -1;
	unsigned char* next = (unsigned char*)out;
	while (count > 0)
	{
		const ULONG chunk = count > kRandomChunkBytes ? (ULONG)kRandomChunkBytes : (ULONG)count;
		if (!BCRYPT_SUCCESS(BCryptGenRandom(NULL, next, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG))) return -1;
		next += chunk;
		count -= chunk;
	}
	return 0;
}
