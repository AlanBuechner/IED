// Objects with static storage (docs/language/06): a static local starts behind a once guard the first time
// control reaches it, and every global, static member and static local that needs destruction is registered
// when its start completes. They are destroyed at exit in the reverse order, as in C++.

#include "PlatformRuntimeC.h"
#include "PlatformSync.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// The compiler's guard (Backend/CodeGen/StaticStorage.cpp reads `state` inline: kOnceDone skips the region).
typedef struct c4_once_guard
{
	volatile int32_t state;
	uint32_t owner;
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
	c4_lock lock;
	c4_condition onceEnded; // a region completed or was abandoned: waiting threads look again
	ExitEntry* exits;
	size_t exitCount;
	size_t exitCapacity;
	int exitHooked;
} StaticStorage;

static StaticStorage g_storage = { C4_LOCK_INIT, C4_CONDITION_INIT, NULL, 0, 0, 0 };

void c4_run_exit_destroy(void (*destroy)(void*), void* object); // StaticStorageShim.cpp

static void fatal(const char* message)
{
	c4_fatal(message);
}

// --- once guards ---------------------------------------------------------------------------------------

int c4_once_begin(c4_once_guard* guard)
{
	const uint32_t self = c4_os_thread_id();
	int run = 0;
	int reentered = 0;
	c4_lock_take(&g_storage.lock);
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
		c4_condition_wait(&g_storage.onceEnded, &g_storage.lock);
	}
	c4_lock_give(&g_storage.lock);
	if (reentered) fatal("a static local's initialization reached its own declaration again");
	return run;
}

static void end_once(c4_once_guard* guard, int32_t state)
{
	c4_lock_take(&g_storage.lock);
	guard->owner = 0;
	__atomic_store_n(&guard->state, state, __ATOMIC_SEQ_CST);
	c4_lock_give(&g_storage.lock);
	c4_condition_wake_all(&g_storage.onceEnded);
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
		c4_lock_take(&g_storage.lock);
		if (!g_storage.exitCount)
		{
			free(g_storage.exits);
			g_storage.exits = NULL;
			g_storage.exitCapacity = 0;
			c4_lock_give(&g_storage.lock);
			return;
		}
		const ExitEntry entry = g_storage.exits[--g_storage.exitCount];
		c4_lock_give(&g_storage.lock);
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
	c4_lock_take(&g_storage.lock);
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
	c4_lock_give(&g_storage.lock);
	if (!room) fatal("out of memory registering an object to destroy at exit");
}
