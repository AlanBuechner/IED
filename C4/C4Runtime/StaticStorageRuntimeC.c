// Objects with static storage (docs/language/06): a static local starts behind a once guard the first time
// control reaches it, and every global, static member and static local that needs destruction is registered
// when its start completes. They are destroyed at exit in the reverse order, as in C++.

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

// The compiler's guard (Backend/CodeGen/StaticStorage.cpp reads `state` inline: kOnceDone skips the region).
typedef struct c4_once_guard
{
	volatile LONG state;
	DWORD owner;
} c4_once_guard;

enum { kOnceNotStarted = 0, kOnceDone = 1, kOnceRunning = 2 };
enum { kFirstExitCapacity = 64 };

typedef struct ExitEntry
{
	void (*destroy)(void*);
	void* object;
} ExitEntry;

typedef struct StaticStorage
{
	SRWLOCK lock;
	CONDITION_VARIABLE onceEnded; // a region completed or was abandoned: waiting threads look again
	ExitEntry* exits;
	size_t exitCount;
	size_t exitCapacity;
	int exitHooked;
} StaticStorage;

static StaticStorage g_storage = { SRWLOCK_INIT, CONDITION_VARIABLE_INIT, NULL, 0, 0, 0 };

void c4_run_exit_destroy(void (*destroy)(void*), void* object); // StaticStorageShim.cpp

static void fatal(const char* message)
{
	fputs(message, stderr);
	exit(1);
}

// --- once guards ---------------------------------------------------------------------------------------

int c4_once_begin(c4_once_guard* guard)
{
	const DWORD self = GetCurrentThreadId();
	int run = 0;
	int reentered = 0;
	AcquireSRWLockExclusive(&g_storage.lock);
	for (;;)
	{
		if (guard->state == kOnceDone) break;
		if (guard->state == kOnceNotStarted)
		{
			guard->state = kOnceRunning;
			guard->owner = self;
			run = 1;
			break;
		}
		if (guard->owner == self)
		{
			reentered = 1;
			break;
		}
		SleepConditionVariableSRW(&g_storage.onceEnded, &g_storage.lock, INFINITE, 0);
	}
	ReleaseSRWLockExclusive(&g_storage.lock);
	if (reentered) fatal("C4: fatal: a static local's initialization reached its own declaration again\n");
	return run;
}

static void end_once(c4_once_guard* guard, LONG state)
{
	AcquireSRWLockExclusive(&g_storage.lock);
	guard->owner = 0;
	InterlockedExchange(&guard->state, state);
	ReleaseSRWLockExclusive(&g_storage.lock);
	WakeAllConditionVariable(&g_storage.onceEnded);
}

void c4_once_end(c4_once_guard* guard)
{
	end_once(guard, kOnceDone);
}

void c4_once_abandon(c4_once_guard* guard)
{
	end_once(guard, kOnceNotStarted);
}

// --- destruction at exit -------------------------------------------------------------------------------

static void run_exit_destroys(void)
{
	for (;;)
	{
		AcquireSRWLockExclusive(&g_storage.lock);
		if (!g_storage.exitCount)
		{
			free(g_storage.exits);
			g_storage.exits = NULL;
			g_storage.exitCapacity = 0;
			ReleaseSRWLockExclusive(&g_storage.lock);
			return;
		}
		const ExitEntry entry = g_storage.exits[--g_storage.exitCount];
		ReleaseSRWLockExclusive(&g_storage.lock);
		c4_run_exit_destroy(entry.destroy, entry.object);
	}
}

static int grow_exits(void)
{
	const size_t capacity = g_storage.exitCapacity ? g_storage.exitCapacity * 2 : kFirstExitCapacity;
	ExitEntry* grown = (ExitEntry*)realloc(g_storage.exits, capacity * sizeof(ExitEntry));
	if (!grown) return 0;
	g_storage.exits = grown;
	g_storage.exitCapacity = capacity;
	return 1;
}

void c4_at_exit(void (*destroy)(void*), void* object)
{
	AcquireSRWLockExclusive(&g_storage.lock);
	if (!g_storage.exitHooked)
	{
		atexit(run_exit_destroys);
		g_storage.exitHooked = 1;
	}
	const int room = g_storage.exitCount < g_storage.exitCapacity || grow_exits();
	if (room)
	{
		g_storage.exits[g_storage.exitCount].destroy = destroy;
		g_storage.exits[g_storage.exitCount].object = object;
		g_storage.exitCount++;
	}
	ReleaseSRWLockExclusive(&g_storage.lock);
	if (!room) fatal("C4: fatal: out of memory registering an object to destroy at exit\n");
}
