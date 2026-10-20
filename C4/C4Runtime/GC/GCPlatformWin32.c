#include "GC/GCPlatform.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <intrin.h>

#pragma comment(lib, "Synchronization.lib")

enum { kMapAttempts = 16 };

static void* map_at(void* address, size_t size)
{
	return VirtualAlloc(address, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void* gc_os_map(size_t size, size_t align)
{
	// Reserve enough to find an aligned start, release it, and map exactly there (another thread may take the
	// range in between, so retry).
	for (int attempt = 0; attempt < kMapAttempts; ++attempt)
	{
		void* probe = VirtualAlloc(NULL, size + align, MEM_RESERVE, PAGE_NOACCESS);
		if (!probe) return NULL;
		uintptr_t aligned = ((uintptr_t)probe + align - 1) & ~(uintptr_t)(align - 1);
		VirtualFree(probe, 0, MEM_RELEASE);
		void* mapped = map_at((void*)aligned, size);
		if (mapped) return mapped;
	}
	return NULL;
}

void gc_os_decommit(void* start, size_t size)
{
	VirtualFree(start, size, MEM_DECOMMIT);
}

int gc_os_commit(void* start, size_t size)
{
	return VirtualAlloc(start, size, MEM_COMMIT, PAGE_READWRITE) != NULL;
}

uint64_t gc_os_now_ns(void)
{
	LARGE_INTEGER freq, count;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&count);
	return (uint64_t)((double)count.QuadPart * 1e9 / (double)freq.QuadPart);
}

// ------------------------------------------------------------------------------------------------
// The collector's memory and lock

static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static HANDLE g_heap;
static HANDLE g_scratch;
static CRITICAL_SECTION g_lock;
static DWORD g_exitSlot;
static BOOLEAN (NTAPI* g_shutdownInProgress)(void);

static void on_fiber_exit(void* data);

static BOOL CALLBACK start_platform(PINIT_ONCE once, PVOID parameter, PVOID* context)
{
	(void)once; (void)parameter; (void)context;
	g_heap = HeapCreate(HEAP_NO_SERIALIZE, 0, 0); // the collector's lock serializes it
	g_scratch = HeapCreate(0, 0, 0);               // its own lock: the collector's helpers use it while the collector's is held
	InitializeCriticalSectionAndSpinCount(&g_lock, 4000);
	g_exitSlot = FlsAlloc(on_fiber_exit);
	g_shutdownInProgress = (BOOLEAN (NTAPI*)(void))(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlDllShutdownInProgress");
	return g_heap != NULL && g_scratch != NULL && g_exitSlot != FLS_OUT_OF_INDEXES;
}

static void platform(void)
{
	if (!InitOnceExecuteOnce(&g_once, start_platform, NULL, NULL)) gc_os_fatal("the collector could not start");
}

void gc_os_lock(void)
{
	platform();
	EnterCriticalSection(&g_lock);
}

void gc_os_unlock(void)
{
	LeaveCriticalSection(&g_lock);
}

void* gc_mem_alloc(size_t size)
{
	gc_os_lock();
	void* p = HeapAlloc(g_heap, HEAP_ZERO_MEMORY, size ? size : 1);
	gc_os_unlock();
	return p;
}

void* gc_mem_realloc(void* p, size_t size)
{
	if (!p) return gc_mem_alloc(size);
	gc_os_lock();
	void* grown = HeapReAlloc(g_heap, HEAP_ZERO_MEMORY, p, size ? size : 1);
	gc_os_unlock();
	return grown;
}

void gc_mem_free(void* p)
{
	if (!p) return;
	gc_os_lock();
	HeapFree(g_heap, 0, p);
	gc_os_unlock();
}

void* gc_os_scratch_realloc(void* p, size_t size)
{
	platform();
	if (!p) return HeapAlloc(g_scratch, 0, size ? size : 1);
	return HeapReAlloc(g_scratch, 0, p, size ? size : 1);
}

void gc_os_scratch_free(void* p)
{
	if (p) HeapFree(g_scratch, 0, p);
}

// ------------------------------------------------------------------------------------------------
// Mark helpers

typedef struct HelperStart
{
	void (*run)(void*);
	void* arg;
} HelperStart;

static DWORD WINAPI helper_main(LPVOID parameter)
{
	HelperStart start = *(HelperStart*)parameter;
	gc_os_scratch_free(parameter);
	start.run(start.arg);
	return 0;
}

int gc_os_start_helper(void (*run)(void*), void* arg)
{
	HelperStart* start = (HelperStart*)gc_os_scratch_realloc(NULL, sizeof(HelperStart));
	if (!start) return 0;
	start->run = run;
	start->arg = arg;
	HANDLE thread = CreateThread(NULL, 0, helper_main, start, 0, NULL);
	if (!thread)
	{
		gc_os_scratch_free(start);
		return 0;
	}
	CloseHandle(thread);
	return 1;
}

uint32_t gc_os_processor_count(void)
{
	const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
	return count ? (uint32_t)count : 1u;
}

void gc_os_relax(void) { _mm_pause(); }

void gc_os_park(volatile long* address, long expected)
{
	WaitOnAddress(address, &expected, sizeof(long), INFINITE);
}

void gc_os_wake_all(volatile long* address)
{
	WakeByAddressAll((PVOID)address);
}

// ------------------------------------------------------------------------------------------------
// Threads

GCOsThread gc_os_this_thread(void)
{
	GCOsThread thread;
	HANDLE handle = NULL;
	const DWORD access = THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION;
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &handle, access, FALSE, 0))
		gc_os_fatal("the collector could not register a thread");
	ULONG_PTR low = 0, high = 0;
	GetCurrentThreadStackLimits(&low, &high);
	thread.handle = handle;
	thread.id = GetCurrentThreadId();
	thread.stackTop = (const void*)high;
	return thread;
}

void gc_os_release_thread(GCOsThread* thread)
{
	if (thread->handle) CloseHandle((HANDLE)thread->handle);
	thread->handle = NULL;
}

uint64_t gc_os_this_thread_id(void) { return GetCurrentThreadId(); }

void gc_os_yield(void) { SwitchToThread(); }

int gc_os_suspend(const GCOsThread* thread)
{
	if (SuspendThread((HANDLE)thread->handle) == (DWORD)-1) return 0;
	// SuspendThread only asks: GetThreadContext returns once the thread has really stopped.
	CONTEXT context;
	memset(&context, 0, sizeof(context));
	context.ContextFlags = CONTEXT_INTEGER;
	GetThreadContext((HANDLE)thread->handle, &context);
	return 1;
}

void gc_os_resume(const GCOsThread* thread)
{
	ResumeThread((HANDLE)thread->handle);
}

static int is_readable(const MEMORY_BASIC_INFORMATION* info)
{
	const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY;
	return info->State == MEM_COMMIT && (info->Protect & readable) && !(info->Protect & PAGE_GUARD);
}

// Only the committed part of the reserved stack can be read; walk it region by region.
static void scan_committed(gc_scan_range_fn scan, void* ctx, uintptr_t from, uintptr_t top)
{
	uintptr_t address = from & ~(uintptr_t)4095;
	while (address < top)
	{
		MEMORY_BASIC_INFORMATION info;
		if (VirtualQuery((LPCVOID)address, &info, sizeof(info)) == 0) return;
		uintptr_t end = (uintptr_t)info.BaseAddress + info.RegionSize;
		if (end > top) end = top;
		uintptr_t begin = address > from ? address : from;
		if (is_readable(&info) && begin < end) scan(ctx, (const void*)begin, (const void*)end);
		if (end <= address) return;
		address = end;
	}
}

void gc_os_scan_stack(gc_scan_range_fn scan, void* ctx)
{
	// Optimized code keeps pointers in callee-saved registers across the call into the collector: capture them
	// here, and start the stack scan at this frame's own locals so a value a prologue saved on the way in is
	// covered too.
	CONTEXT registers;
	memset(&registers, 0, sizeof(registers));
	RtlCaptureContext(&registers);
	scan(ctx, &registers, (const uint8_t*)&registers + sizeof(registers));

	const uintptr_t teb = __readgsqword(0x30);
	const uintptr_t stackTop = *(const uintptr_t*)(teb + 0x8);
	const uintptr_t stackLimit = *(const uintptr_t*)(teb + 0x10);
	uintptr_t from = (uintptr_t)&registers + sizeof(registers);
	if (from < stackLimit || from >= stackTop) return;
	scan_committed(scan, ctx, from, stackTop);
}

void gc_os_scan_suspended(const GCOsThread* thread, gc_scan_range_fn scanNow, gc_scan_range_fn scanStack, void* ctx)
{
	// Every register may hold a pointer, the vector ones too when optimized code spills to them.
	CONTEXT registers;
	memset(&registers, 0, sizeof(registers));
	registers.ContextFlags = CONTEXT_ALL;
	if (!GetThreadContext((HANDLE)thread->handle, &registers)) gc_os_fatal("the collector could not read a stopped thread");
	scanNow(ctx, &registers, (const uint8_t*)&registers + sizeof(registers));
	const uintptr_t top = (uintptr_t)thread->stackTop;
	if (registers.Rsp < top) scan_committed(scanStack, ctx, (uintptr_t)registers.Rsp, top);
}

typedef struct ExitCallback
{
	void (*on_exit)(void*);
	void* data;
} ExitCallback;

static int process_exiting(void)
{
	return g_shutdownInProgress && g_shutdownInProgress();
}

static void on_fiber_exit(void* data)
{
	// Threads the process's end killed may hold any lock: an ending process needs no bookkeeping.
	ExitCallback* callback = (ExitCallback*)data;
	if (!callback || process_exiting()) return;
	callback->on_exit(callback->data);
	gc_mem_free(callback);
}

void gc_os_on_thread_exit(void (*on_exit)(void*), void* data)
{
	platform();
	ExitCallback* callback = (ExitCallback*)gc_mem_alloc(sizeof(ExitCallback));
	callback->on_exit = on_exit;
	callback->data = data;
	FlsSetValue(g_exitSlot, callback);
}

void gc_os_fatal(const char* message)
{
	fprintf(stderr, "C4: fatal: %s\n", message);
	fflush(stderr);
	exit(1);
}
