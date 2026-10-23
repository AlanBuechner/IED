// A script's exits (docs/runtime/04 section 5): its globals' and static locals' destructions run when it unloads;
// its thread_locals' on each thread when that thread ends or the script unloads, whichever comes first. One lock
// guards every script's lists: they change only as objects start, threads end and scripts unload.

#include "PlatformRuntimeC.h"
#include "ScriptRuntimeC.h"
#include "ThreadRuntimeC.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

enum { kFirstExitCapacity = 16 };

void c4_run_exit_destroy(void (*destroy)(void*), void* object); // StaticStorageShim.cpp

typedef struct ScriptExit
{
	void (*destroy)(void*);
	void* object;
} ScriptExit;

typedef struct ScriptExits
{
	ScriptExit* entries;
	size_t count;
	size_t capacity;
} ScriptExits;

// One thread's thread_locals of one script. The thread's exit and the script's unload each hold a reference: the
// first to come runs the destructions, the last frees the block.
typedef struct ScriptThreadExits
{
	ScriptExits exits;
	uint64_t thread;
	int32_t done;
	int32_t references;
	struct ScriptThreadExits* next;
} ScriptThreadExits;

struct c4_script_module
{
	ScriptExits exits;
	ScriptThreadExits* threads;
	int32_t finished;
};

static SRWLOCK g_lock = SRWLOCK_INIT;

static void fatal(const char* message)
{
	c4_fatal(message);
}

static void push_exit(ScriptExits* exits, void (*destroy)(void*), void* object)
{
	if (exits->count == exits->capacity)
	{
		const size_t capacity = exits->capacity ? exits->capacity * 2 : kFirstExitCapacity;
		ScriptExit* grown = (ScriptExit*)realloc(exits->entries, capacity * sizeof(ScriptExit));
		if (!grown) fatal("out of memory registering a script object to destroy");
		exits->entries = grown;
		exits->capacity = capacity;
	}
	exits->entries[exits->count].destroy = destroy;
	exits->entries[exits->count].object = object;
	exits->count++;
}

// Runs and frees `exits`, the last registered first; the caller owns them and holds no lock.
static void run_exits(ScriptExits exits)
{
	while (exits.count)
	{
		const ScriptExit exit = exits.entries[--exits.count];
		c4_run_exit_destroy(exit.destroy, exit.object);
	}
	free(exits.entries);
}

// Takes a block's destructions to run, once; empty when they ran already.
static ScriptExits take_thread_exits(ScriptThreadExits* block)
{
	ScriptExits none = { 0 };
	if (block->done) return none;
	block->done = 1;
	const ScriptExits taken = block->exits;
	block->exits = none;
	return taken;
}

static void release_block(ScriptThreadExits* block)
{
	if (--block->references == 0) free(block);
}

// The thread's exit: the script may be gone already, so the block is all it touches.
static void run_thread_block(void* item)
{
	ScriptThreadExits* block = (ScriptThreadExits*)item;
	AcquireSRWLockExclusive(&g_lock);
	const ScriptExits exits = take_thread_exits(block);
	ReleaseSRWLockExclusive(&g_lock);
	run_exits(exits);
	AcquireSRWLockExclusive(&g_lock);
	release_block(block);
	ReleaseSRWLockExclusive(&g_lock);
}

static ScriptThreadExits* thread_block(c4_script_module* module, uint64_t thread)
{
	for (ScriptThreadExits* block = module->threads; block; block = block->next)
		if (block->thread == thread && !block->done) return block;
	return NULL;
}

c4_script_module* c4_script_module_create(void)
{
	return (c4_script_module*)calloc(1, sizeof(c4_script_module));
}

void c4_script_at_exit(c4_script_module* module, void (*destroy)(void*), void* object)
{
	if (!module) return;
	AcquireSRWLockExclusive(&g_lock);
	if (!module->finished) push_exit(&module->exits, destroy, object);
	ReleaseSRWLockExclusive(&g_lock);
}

void c4_script_thread_at_exit(c4_script_module* module, void (*destroy)(void*), void* object)
{
	if (!module) return;
	const uint64_t thread = c4_thread_current_id();
	ScriptThreadExits* fresh = NULL;
	AcquireSRWLockExclusive(&g_lock);
	if (!module->finished)
	{
		ScriptThreadExits* block = thread_block(module, thread);
		if (!block)
		{
			block = (ScriptThreadExits*)calloc(1, sizeof(ScriptThreadExits));
			if (!block) fatal("out of memory registering a script thread_local");
			block->thread = thread;
			block->references = 2;
			block->next = module->threads;
			module->threads = block;
			fresh = block;
		}
		push_exit(&block->exits, destroy, object);
	}
	ReleaseSRWLockExclusive(&g_lock);
	if (fresh) c4_thread_at_exit(run_thread_block, fresh);
}

void c4_script_module_finish(c4_script_module* module)
{
	if (!module) return;
	AcquireSRWLockExclusive(&g_lock);
	module->finished = 1;
	ReleaseSRWLockExclusive(&g_lock);
	for (ScriptThreadExits* block = module->threads; block; block = block->next)
	{
		AcquireSRWLockExclusive(&g_lock);
		const ScriptExits exits = take_thread_exits(block);
		ReleaseSRWLockExclusive(&g_lock);
		run_exits(exits);
	}
	AcquireSRWLockExclusive(&g_lock);
	const ScriptExits exits = module->exits;
	module->exits.entries = NULL;
	module->exits.count = module->exits.capacity = 0;
	ReleaseSRWLockExclusive(&g_lock);
	run_exits(exits);
}

void c4_script_module_destroy(c4_script_module* module)
{
	if (!module) return;
	AcquireSRWLockExclusive(&g_lock);
	ScriptThreadExits* block = module->threads;
	while (block)
	{
		ScriptThreadExits* next = block->next;
		release_block(block);
		block = next;
	}
	free(module->exits.entries);
	ReleaseSRWLockExclusive(&g_lock);
	free(module);
}
