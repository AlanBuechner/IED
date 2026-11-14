// Time for Bedrock's Platform namespace (PlatformRuntimeC.h): CLOCK_MONOTONIC as the counter (a tick is a nanosecond),
// CLOCK_REALTIME as the wall clock, and the local time zone's offset as the C library has it (TZ, else /etc/localtime,
// read once when first asked: a zone changed later is not seen).

#include "PlatformRuntimeC.h"
#include <time.h>

enum { kNanosecondsPerSecond = 1000000000 };

static int64_t Nanoseconds(clockid_t clock)
{
	struct timespec now;
	clock_gettime(clock, &now);
	return (int64_t)now.tv_sec * kNanosecondsPerSecond + now.tv_nsec;
}

int64_t c4_time_ticks(void) { return Nanoseconds(CLOCK_MONOTONIC); }

int64_t c4_time_tick_frequency(void) { return kNanosecondsPerSecond; }

int64_t c4_time_unix_ns(void) { return Nanoseconds(CLOCK_REALTIME); }

// The zone's standard offset: for an instant the C library's conversion refuses.
static int32_t StandardOffsetSeconds(void)
{
	tzset();
	return (int32_t)-timezone;
}

static int64_t FloorDivide(int64_t value, int64_t divisor)
{
	const int64_t quotient = value / divisor;
	return quotient * divisor > value ? quotient - 1 : quotient;
}

int32_t c4_time_local_offset_seconds(int64_t unixNs)
{
	const time_t seconds = (time_t)FloorDivide(unixNs, kNanosecondsPerSecond);
	struct tm local;
	if (!localtime_r(&seconds, &local)) return StandardOffsetSeconds();
	return (int32_t)local.tm_gmtoff;
}
