// The monotonic clock behind clock_now_ns(): nanoseconds since an arbitrary start, never going back, unaffected by
// changes to the system time. Frame timing, timeouts and the test harness read it.

#include <windows.h>
#include <stdint.h>

enum { kNanosecondsPerSecond = 1000000000 };

int64_t c4_clock_now_ns(void)
{
	LARGE_INTEGER frequency, count;
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&count);
	// Whole seconds and the rest apart, so the product never overflows.
	const int64_t seconds = count.QuadPart / frequency.QuadPart;
	const int64_t rest = count.QuadPart % frequency.QuadPart;
	return seconds * kNanosecondsPerSecond + rest * kNanosecondsPerSecond / frequency.QuadPart;
}
