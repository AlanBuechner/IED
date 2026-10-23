// Objects with thread storage (docs/language/04 4.13): each thread's thread_local objects are registered as they
// start and destroyed when the thread ends, the last started first. The main thread's join the process's exit list
// instead (StaticStorageRuntimeC.c), so they end in the reverse order of every start, among the globals'.

#include "PlatformRuntimeC.h"
#include "ThreadRuntimeC.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

enum { kFirstExitCapacity = 16 };

typedef struct ThreadExit
{
	void (*destroy)(void*);
	void* object;
} ThreadExit;

typedef struct ThreadExits
{
	ThreadExit* entries;
	size_t count;
	size_t capacity;
} ThreadExits;

static _Thread_local ThreadExits t_exits;
static DWORD g_mainThread;

void c4_at_exit(void (*destroy)(void*), void* object);                   // StaticStorageRuntimeC.c
void c4_run_exit_destroy(void (*destroy)(void*), void* object);          // StaticStorageShim.cpp
void c4_gc_register_thread(void);

static void fatal(const char* message)
{
	c4_fatal(message);
}

// The thread that runs the C runtime's initializers is the main thread.
static int note_main_thread(void)
{
	g_mainThread = GetCurrentThreadId();
	return 0;
}
__attribute__((section(".CRT$XIU"), used)) static int (*const g_noteMainThread)(void) = note_main_thread;

bool c4_thread_is_main(void) { return GetCurrentThreadId() == g_mainThread; }

static int grow(ThreadExits* exits)
{
	const size_t capacity = exits->capacity ? exits->capacity * 2 : kFirstExitCapacity;
	ThreadExit* grown = (ThreadExit*)realloc(exits->entries, capacity * sizeof(ThreadExit));
	if (!grown) return 0;
	exits->entries = grown;
	exits->capacity = capacity;
	return 1;
}

void c4_thread_at_exit(void (*destroy)(void*), void* object)
{
	if (c4_thread_is_main())
	{
		c4_at_exit(destroy, object);
		return;
	}
	c4_gc_register_thread(); // the collector ends the thread's C4 life, these destructions first
	ThreadExits* exits = &t_exits;
	if (exits->count == exits->capacity && !grow(exits)) fatal("out of memory registering an object to destroy at thread end");
	exits->entries[exits->count].destroy = destroy;
	exits->entries[exits->count].object = object;
	exits->count++;
}

void c4_thread_run_exits(void)
{
	// A destruction may start another thread_local, which registers again: take one at a time.
	ThreadExits* exits = &t_exits;
	while (exits->count)
	{
		const ThreadExit entry = exits->entries[--exits->count];
		c4_run_exit_destroy(entry.destroy, entry.object);
	}
	free(exits->entries);
	exits->entries = NULL;
	exits->capacity = 0;
}

void c4_thread_once_abandon(int32_t* guard) { *guard = 0; }

void c4_thread_once_reentered(void)
{
	fatal("a static local's initialization reached its own declaration again");
}
