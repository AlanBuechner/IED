// Time for Bedrock's Platform namespace (PlatformRuntimeC.h): the performance counter, the wall clock and the local
// time zone's offset.

#include "PlatformRuntimeC.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum { kNanosecondsPerFileTimeTick = 100, kSecondsPerMinute = 60 };
// FILETIME counts 100 ns ticks from 1601-01-01; this many of them lie before 1970-01-01.
static const int64_t kFileTimeTicksBeforeUnixEpoch = 116444736000000000LL;
static const int64_t kFileTimeTicksPerSecond = 10000000LL;

int64_t c4_time_ticks(void)
{
	LARGE_INTEGER count;
	QueryPerformanceCounter(&count);
	return count.QuadPart;
}

int64_t c4_time_tick_frequency(void)
{
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	return frequency.QuadPart;
}

static int64_t FileTimeTicks(FILETIME time) { return (int64_t)(((uint64_t)time.dwHighDateTime << 32) | time.dwLowDateTime); }

static FILETIME ToFileTime(int64_t ticks)
{
	FILETIME time;
	time.dwLowDateTime = (DWORD)((uint64_t)ticks & 0xFFFFFFFFu);
	time.dwHighDateTime = (DWORD)((uint64_t)ticks >> 32);
	return time;
}

int64_t c4_time_unix_ns(void)
{
	FILETIME now;
	GetSystemTimePreciseAsFileTime(&now);
	return (FileTimeTicks(now) - kFileTimeTicksBeforeUnixEpoch) * kNanosecondsPerFileTimeTick;
}

// The zone's standard offset: for an instant the system's conversion refuses.
static int32_t StandardOffsetSeconds(void)
{
	TIME_ZONE_INFORMATION zone;
	GetTimeZoneInformation(&zone);
	return -(int32_t)zone.Bias * kSecondsPerMinute;
}

static int64_t FloorDivide(int64_t value, int64_t divisor)
{
	const int64_t quotient = value / divisor;
	return quotient * divisor > value ? quotient - 1 : quotient;
}

int32_t c4_time_local_offset_seconds(int64_t unixNs)
{
	const int64_t utcTicks = FloorDivide(unixNs, kNanosecondsPerFileTimeTick) + kFileTimeTicksBeforeUnixEpoch;
	const FILETIME utcFile = ToFileTime(utcTicks);
	SYSTEMTIME utc, local;
	FILETIME localFile;
	if (!FileTimeToSystemTime(&utcFile, &utc)) return StandardOffsetSeconds();
	if (!SystemTimeToTzSpecificLocalTimeEx(NULL, &utc, &local) || !SystemTimeToFileTime(&local, &localFile))
		return StandardOffsetSeconds();
	// SYSTEMTIME drops the ticks below a millisecond, so whole seconds are compared.
	return (int32_t)(FileTimeTicks(localFile) / kFileTimeTicksPerSecond - utcTicks / kFileTimeTicksPerSecond);
}
