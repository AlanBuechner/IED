// Threads on Windows (ThreadRuntimeC.h; ThreadRuntimeC.c has what every system shares). Waiting on an address
// (WaitOnAddress) is the one blocking primitive: Bedrock's lock-free library parks on it when there is nothing to do.

#include "ThreadRuntimeC.h"
#include "ThreadStart.h"
#include <windows.h>
#include <intrin.h>
#include <process.h>
#include <stdint.h>
#include <stdlib.h>

#pragma comment(lib, "Synchronization.lib")

typedef struct C4Thread
{
	HANDLE handle;
} C4Thread;

static unsigned __stdcall thread_main(void* packet)
{
	c4_thread_run_packet(packet);
	return 0;
}

// A thread running entry(state), or NULL when the system starts none. `state` stays alive until the thread runs.
void* c4_thread_start(void* entry, void* state)
{
	C4Thread* thread = (C4Thread*)malloc(sizeof(C4Thread));
	void* packet = thread ? c4_thread_packet(entry, state) : NULL;
	if (!packet)
	{
		free(thread);
		return NULL;
	}
	thread->handle = (HANDLE)_beginthreadex(NULL, 0, thread_main, packet, 0, NULL);
	if (!thread->handle)
	{
		c4_thread_drop_packet(packet);
		free(thread);
		return NULL;
	}
	return thread;
}

// Waits for the thread to end; the handle is gone afterwards.
void c4_thread_join(void* thread)
{
	if (!thread) return;
	C4Thread* t = (C4Thread*)thread;
	WaitForSingleObject(t->handle, INFINITE);
	CloseHandle(t->handle);
	free(t);
}

// Lets the thread run on alone; the handle is gone afterwards.
void c4_thread_detach(void* thread)
{
	if (!thread) return;
	C4Thread* t = (C4Thread*)thread;
	CloseHandle(t->handle);
	free(t);
}

uint64_t c4_thread_current_id(void) { return GetCurrentThreadId(); }

enum { kMostCores = 64 };   // one processor group's

// The logical processors of its group this process may run on (its affinity); 0 on a system of several groups, where a process may span them.
static ULONG_PTR ProcessProcessors(void)
{
	if (GetActiveProcessorGroupCount() != 1) return 0;
	DWORD_PTR process = 0;
	DWORD_PTR system = 0;
	return GetProcessAffinityMask(GetCurrentProcess(), &process, &system) ? (ULONG_PTR)process : 0;
}

static uint32_t CountBits(ULONG_PTR bits)
{
	uint32_t count = 0;
	for (; bits; bits &= bits - 1) count += 1;
	return count;
}

// Each core's logical processors this process may run on, in the system's order (a core with none left out); how many cores.
static int32_t UsableCores(ULONG_PTR cores[kMostCores])
{
	DWORD bytes = 0;
	GetLogicalProcessorInformation(NULL, &bytes);
	SYSTEM_LOGICAL_PROCESSOR_INFORMATION* infos = bytes ? (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)malloc(bytes) : NULL;
	int32_t found = 0;
	if (infos && GetLogicalProcessorInformation(infos, &bytes))
	{
		const ULONG_PTR mine = ProcessProcessors();
		const DWORD count = bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
		for (DWORD i = 0; i < count && found < kMostCores; i += 1)
		{
			if (infos[i].Relationship != RelationProcessorCore) continue;
			const ULONG_PTR usable = mine ? infos[i].ProcessorMask & mine : infos[i].ProcessorMask;
			if (usable) cores[found++] = usable;
		}
	}
	free(infos);
	return found;
}

// The processors this process may run on: a pool sized by the system's puts more threads than processors on a process kept to fewer (BUG-454).
uint32_t c4_thread_hardware_count(void)
{
	const ULONG_PTR mine = ProcessProcessors();
	if (mine) return CountBits(mine);
	const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
	return count ? (uint32_t)count : 1u;
}

uint32_t c4_thread_core_count(void)
{
	ULONG_PTR cores[kMostCores];
	const int32_t found = UsableCores(cores);
	return found > 0 ? (uint32_t)found : c4_thread_hardware_count();
}

void c4_thread_yield(void) { SwitchToThread(); }

void c4_cpu_relax(void) { _mm_pause(); }

static DWORD milliseconds(int64_t ns)
{
	if (ns < 0) return INFINITE;
	const int64_t ms = (ns + 999999) / 1000000;
	return ms >= (int64_t)INFINITE ? INFINITE - 1 : (DWORD)ms;
}

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// Each thread sleeps on a high-resolution waitable timer of its own (Windows 10 1803 and later): it ends within about
// half a millisecond of its time, where Sleep ends on the system's tick (15.6 ms unless a process raised the timer
// resolution), so a frame limiter that waits 3 ms waited 15.6. Systems without such timers fall back to Sleep.
static _Thread_local HANDLE t_sleepTimer;

void c4_thread_sleep_ns(int64_t ns)
{
	if (ns <= 0)
	{
		Sleep(0);
		return;
	}
	if (!t_sleepTimer) t_sleepTimer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	LARGE_INTEGER due;
	due.QuadPart = -((ns + 99) / 100); // relative, in 100 ns units
	if (t_sleepTimer && SetWaitableTimer(t_sleepTimer, &due, 0, NULL, NULL, FALSE))
	{
		WaitForSingleObject(t_sleepTimer, INFINITE);
		return;
	}
	Sleep(milliseconds(ns));
}

// The calling thread on one logical processor of core number `core` of those this process may run on, so that no two threads pinned to
// different cores share one through its hyperthreads. -1 when there is no such core.
int32_t c4_thread_pin_to_core(int32_t core)
{
	if (core < 0) return -1;
	ULONG_PTR cores[kMostCores];
	if (core >= UsableCores(cores)) return -1;
	const ULONG_PTR mask = cores[core];
	return SetThreadAffinityMask(GetCurrentThread(), mask & (~mask + 1)) ? 0 : -1;
}

int32_t c4_thread_set_name(const char* name)
{
	if (!name) return -1;
	const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, NULL, 0);
	if (wideLength <= 0) return -1;
	wchar_t* wide = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)wideLength);
	if (!wide) return -1;
	MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, wideLength);
	const HRESULT named = SetThreadDescription(GetCurrentThread(), wide);
	free(wide);
	return SUCCEEDED(named) ? 0 : -1;
}

int32_t c4_thread_name(char* out, int32_t cap)
{
	if (!out || cap <= 0) return -1;
	PWSTR wide = NULL;
	if (FAILED(GetThreadDescription(GetCurrentThread(), &wide))) return -1;
	const int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, cap, NULL, NULL);
	LocalFree(wide);
	return length > 0 ? length - 1 : -1;
}

// Blocks while the `size` bytes at `address` equal those at `expected`, until a wake or `timeoutNs` (negative:
// none); false when the timeout passed. It may also return early, so callers check their condition again.
bool c4_wait_on_address(void* address, void* expected, uint64_t size, int64_t timeoutNs)
{
	if (WaitOnAddress((volatile VOID*)address, expected, (SIZE_T)size, milliseconds(timeoutNs))) return true;
	return GetLastError() != ERROR_TIMEOUT;
}

void c4_wake_one(void* address) { WakeByAddressSingle(address); }
void c4_wake_all(void* address) { WakeByAddressAll(address); }
