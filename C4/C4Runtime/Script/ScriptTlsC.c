// Emulated thread storage for script code (LLVM's emutls ABI, docs/runtime/04 section 4.2). A variable gets a slot
// on its first use; each thread keeps an array of its objects by slot, made on that thread's first use of the
// variable. The slow path, the threads' list and releasing take one lock; reading an existing object takes none.

#include "ScriptRuntimeC.h"
#include "GCRuntimeC.h"
#include "ThreadRuntimeC.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef struct ThreadSlots
{
	void** objects;            // by slot; null where this thread has no object yet
	size_t capacity;
	struct ThreadSlots* next;  // every thread's slots, for releasing a variable everywhere
} ThreadSlots;

static SRWLOCK g_lock = SRWLOCK_INIT;
static ThreadSlots* g_threads;
static uintptr_t g_nextIndex = 1;
static uintptr_t* g_freeIndexes;
static size_t g_freeCount;
static size_t g_freeCapacity;
static _Thread_local ThreadSlots* t_slots;

static void fatal(const char* message)
{
	fputs(message, stderr);
	exit(1);
}

static void free_object(void* object)
{
	c4_gc_unregister_region(object);
	_aligned_free(object);
}

static uintptr_t take_index(void)
{
	if (g_freeCount) return g_freeIndexes[--g_freeCount];
	return g_nextIndex++;
}

static void give_back_index(uintptr_t index)
{
	if (g_freeCount == g_freeCapacity)
	{
		const size_t capacity = g_freeCapacity ? g_freeCapacity * 2 : 16;
		uintptr_t* grown = (uintptr_t*)realloc(g_freeIndexes, capacity * sizeof(uintptr_t));
		if (!grown) return; // the slot is not reused
		g_freeIndexes = grown;
		g_freeCapacity = capacity;
	}
	g_freeIndexes[g_freeCount++] = index;
}

// The thread's end: every object it has goes. Script thread_locals were destroyed before (their exits were
// registered after this).
static void release_thread(void* item)
{
	ThreadSlots* slots = (ThreadSlots*)item;
	AcquireSRWLockExclusive(&g_lock);
	for (ThreadSlots** link = &g_threads; *link; link = &(*link)->next)
		if (*link == slots) { *link = slots->next; break; }
	for (size_t i = 0; i < slots->capacity; i++)
		if (slots->objects[i]) free_object(slots->objects[i]);
	ReleaseSRWLockExclusive(&g_lock);
	free(slots->objects);
	free(slots);
	t_slots = NULL;
}

static ThreadSlots* this_thread_slots(void)
{
	if (t_slots) return t_slots;
	ThreadSlots* slots = (ThreadSlots*)calloc(1, sizeof(ThreadSlots));
	if (!slots) fatal("C4: fatal: out of memory for a script thread_local\n");
	AcquireSRWLockExclusive(&g_lock);
	slots->next = g_threads;
	g_threads = slots;
	ReleaseSRWLockExclusive(&g_lock);
	t_slots = slots;
	c4_thread_at_exit(release_thread, slots);
	return slots;
}

static uintptr_t index_of(__emutls_control* control)
{
	uintptr_t index = __atomic_load_n(&control->index, __ATOMIC_ACQUIRE);
	if (index) return index;
	AcquireSRWLockExclusive(&g_lock);
	index = control->index;
	if (!index)
	{
		index = take_index();
		__atomic_store_n(&control->index, index, __ATOMIC_RELEASE);
	}
	ReleaseSRWLockExclusive(&g_lock);
	return index;
}

static void* new_object(const __emutls_control* control)
{
	const size_t align = control->align > sizeof(void*) ? control->align : sizeof(void*);
	const size_t size = control->size ? control->size : 1;
	void* object = _aligned_malloc(size, align);
	if (!object) fatal("C4: fatal: out of memory for a script thread_local\n");
	if (control->value) memcpy(object, control->value, control->size);
	else memset(object, 0, size);
	c4_gc_register_region(object, size); // it may hold managed pointers
	return object;
}

static void* make_object(__emutls_control* control, uintptr_t index)
{
	ThreadSlots* slots = this_thread_slots();
	void* object = new_object(control);
	AcquireSRWLockExclusive(&g_lock);
	if (index > slots->capacity)
	{
		size_t capacity = slots->capacity ? slots->capacity : 8;
		while (capacity < index) capacity *= 2;
		void** grown = (void**)realloc(slots->objects, capacity * sizeof(void*));
		if (!grown) fatal("C4: fatal: out of memory for a script thread_local\n");
		memset(grown + slots->capacity, 0, (capacity - slots->capacity) * sizeof(void*));
		slots->objects = grown;
		slots->capacity = capacity;
	}
	slots->objects[index - 1] = object;
	ReleaseSRWLockExclusive(&g_lock);
	return object;
}

void* __emutls_get_address(__emutls_control* control)
{
	const uintptr_t index = index_of(control);
	ThreadSlots* slots = t_slots;
	if (slots && index <= slots->capacity && slots->objects[index - 1]) return slots->objects[index - 1];
	return make_object(control, index);
}

void c4_script_tls_release(__emutls_control* const* controls, uint32_t count)
{
	AcquireSRWLockExclusive(&g_lock);
	for (uint32_t c = 0; c < count; c++)
	{
		const uintptr_t index = controls[c] ? controls[c]->index : 0;
		if (!index) continue;
		for (ThreadSlots* slots = g_threads; slots; slots = slots->next)
		{
			if (index > slots->capacity || !slots->objects[index - 1]) continue;
			free_object(slots->objects[index - 1]);
			slots->objects[index - 1] = NULL;
		}
		controls[c]->index = 0;
		give_back_index(index);
	}
	ReleaseSRWLockExclusive(&g_lock);
}
