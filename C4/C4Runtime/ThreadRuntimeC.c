// Threads for the program and Bedrock (D-48, ThreadRuntimeC.h). A thread started here attaches before its entry
// runs. Waiting on an address (WaitOnAddress) is the one blocking primitive: Bedrock's lock-free library parks on
// it when there is nothing to do.

#include "GCRuntimeC.h"
#include "ThreadRuntimeC.h"
#include <windows.h>
#include <intrin.h>
#include <process.h>
#include <stdint.h>
#include <stdlib.h>

#pragma comment(lib, "Synchronization.lib")

typedef void (*c4_thread_entry)(void* state);

void __c4_thread_start(void); // the program's thread_local starts (CodeGen/CodeGenDriver.cpp)
void c4_install_terminate(void); // EHRuntime.cpp: an uncaught exception ends the process cleanly on this thread too

static _Thread_local int t_attached;

void c4_thread_attach(void)
{
	if (t_attached) return;
	t_attached = 1;
	c4_install_terminate();
	c4_gc_register_thread();
	__c4_thread_start();
}

typedef struct StartPacket
{
	c4_thread_entry entry;
	void* state;
} StartPacket;

typedef struct C4Thread
{
	HANDLE handle;
} C4Thread;

static unsigned __stdcall thread_main(void* parameter)
{
	StartPacket packet = *(StartPacket*)parameter;
	free(parameter);
	c4_thread_attach();
	c4_gc_unpin(packet.state); // this thread's stack holds it now
	packet.entry(packet.state);
	return 0;
}

// A thread running entry(state), or NULL when the system starts none. `state` stays alive until the thread runs.
void* c4_thread_start(void* entry, void* state)
{
	StartPacket* packet = (StartPacket*)malloc(sizeof(StartPacket));
	C4Thread* thread = (C4Thread*)malloc(sizeof(C4Thread));
	if (!packet || !thread)
	{
		free(packet);
		free(thread);
		return NULL;
	}
	packet->entry = (c4_thread_entry)entry;
	packet->state = state;
	c4_gc_pin(state);
	thread->handle = (HANDLE)_beginthreadex(NULL, 0, thread_main, packet, 0, NULL);
	if (!thread->handle)
	{
		c4_gc_unpin(state);
		free(packet);
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

uint32_t c4_thread_hardware_count(void)
{
	const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
	return count ? (uint32_t)count : 1u;
}

void c4_thread_yield(void) { SwitchToThread(); }

void c4_cpu_relax(void) { _mm_pause(); }

static DWORD milliseconds(int64_t ns)
{
	if (ns < 0) return INFINITE;
	const int64_t ms = (ns + 999999) / 1000000;
	return ms >= (int64_t)INFINITE ? INFINITE - 1 : (DWORD)ms;
}

void c4_thread_sleep_ns(int64_t ns) { Sleep(milliseconds(ns < 0 ? 0 : ns)); }

// Blocks while the `size` bytes at `address` equal those at `expected`, until a wake or `timeoutNs` (negative:
// none); false when the timeout passed. It may also return early, so callers check their condition again.
bool c4_wait_on_address(void* address, void* expected, uint64_t size, int64_t timeoutNs)
{
	if (WaitOnAddress((volatile VOID*)address, expected, (SIZE_T)size, milliseconds(timeoutNs))) return true;
	return GetLastError() != ERROR_TIMEOUT;
}

void c4_wake_one(void* address) { WakeByAddressSingle(address); }
void c4_wake_all(void* address) { WakeByAddressAll(address); }
