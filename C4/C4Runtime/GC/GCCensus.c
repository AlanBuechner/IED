#include "GC/GCCensus.h"
#include "GC/GCPlatform.h"
#include <stdlib.h>
#include <string.h>

// The entries of a census being taken, one per type record, found by its address (open addressing).
typedef struct Tally
{
	c4_gc_census_entry* entries;
	const c4_gc_type** types;  // each entry's record
	int32_t count;
	int32_t capacity;
	int32_t* slots;          // entry index + 1, 0: empty
	uint32_t slotMask;
} Tally;

bool gc_census_set(GCCensus* census, bool on)
{
	const bool was = census->on != 0;
	if (on && !was) __atomic_add_fetch(&census->epoch, 1u, __ATOMIC_RELEASE);
	__atomic_store_n(&census->on, on ? 1 : 0, __ATOMIC_RELEASE);
	return was;
}

bool gc_census_prepare(GCSpan* span, uint32_t epoch)
{
	if (!span->tags) span->tags = (const c4_gc_type**)gc_mem_alloc(span->nelems * sizeof(const c4_gc_type*));
	else memset((void*)span->tags, 0, span->nelems * sizeof(const c4_gc_type*));
	if (!span->tags) return false;
	span->tagsEpoch = epoch;
	return true;
}

static uint32_t slot_of(const c4_gc_type* type, uint32_t mask)
{
	uint64_t h = (uint64_t)(uintptr_t)type * 0x9E3779B97F4A7C15ULL;
	return (uint32_t)(h >> 40) & mask;
}

static void rehash(Tally* tally, uint32_t slotCount)
{
	gc_mem_free(tally->slots);
	tally->slots = (int32_t*)gc_mem_alloc(slotCount * sizeof(int32_t));
	tally->slotMask = slotCount - 1u;
	for (int32_t i = 0; i < tally->count; ++i)
	{
		uint32_t s = slot_of(tally->types[i], tally->slotMask);
		while (tally->slots[s]) s = (s + 1u) & tally->slotMask;
		tally->slots[s] = i + 1;
	}
}

// The entry of `type`, added when it is new.
static c4_gc_census_entry* entry_of(Tally* tally, const c4_gc_type* type)
{
	uint32_t s = slot_of(type, tally->slotMask);
	for (; tally->slots[s]; s = (s + 1u) & tally->slotMask)
		if (tally->types[tally->slots[s] - 1] == type) return &tally->entries[tally->slots[s] - 1];
	if (tally->count == tally->capacity)
	{
		tally->capacity *= 2;
		tally->entries = (c4_gc_census_entry*)gc_mem_realloc(tally->entries, (size_t)tally->capacity * sizeof(c4_gc_census_entry));
		tally->types = (const c4_gc_type**)gc_mem_realloc(tally->types, (size_t)tally->capacity * sizeof(const c4_gc_type*));
	}
	c4_gc_census_entry* entry = &tally->entries[tally->count];
	entry->type = type ? type->name : NULL;
	tally->types[tally->count] = type;
	tally->slots[s] = ++tally->count;
	if ((uint32_t)tally->count * 2u > tally->slotMask) rehash(tally, (tally->slotMask + 1u) * 2u);
	return entry;
}

// A span whose table is not this census's holds only objects of no known type: counted at once.
static void count_untyped_span(Tally* tally, const GCSpan* span)
{
	uint64_t objects = 0;
	for (uint32_t w = 0; w < GC_SPAN_WORDS; ++w) objects += (uint64_t)__builtin_popcountll(span->allocBits[w]);
	if (objects == 0) return;
	c4_gc_census_entry* entry = entry_of(tally, NULL);
	entry->objects += objects;
	entry->bytes += objects * span->elemSize;
}

// What the object at `object` says it holds; it may be in the middle of a change on a thread that stopped there.
static uint64_t native_bytes(const c4_gc_type* type, const void* object)
{
	const int64_t bytes = gc_os_count_guarded(type->nativeBytes, object);
	return bytes > 0 ? (uint64_t)bytes : 0;
}

static void count_span(Tally* tally, const GCSpan* span, uint32_t epoch)
{
	if (!span->tags || span->tagsEpoch != epoch)
	{
		count_untyped_span(tally, span);
		return;
	}
	const c4_gc_type* const* tags = span->tags;
	const c4_gc_type* current = NULL;
	c4_gc_census_entry* entry = NULL;
	for (uint32_t w = 0; w < GC_SPAN_WORDS; ++w)
		for (uint64_t bits = span->allocBits[w]; bits; bits &= bits - 1u)
		{
			const uint32_t slot = w * 64u + (uint32_t)__builtin_ctzll(bits);
			const c4_gc_type* type = tags[slot];
			if (!entry || type != current) entry = entry_of(tally, current = type);
			entry->objects += 1;
			entry->bytes += span->elemSize;
			if (type && type->nativeBytes) entry->nativeBytes += native_bytes(type, gc_span_slot_base(span, slot));
		}
}

static int by_name(const void* a, const void* b)
{
	const char* x = ((const c4_gc_census_entry*)a)->type;
	const char* y = ((const c4_gc_census_entry*)b)->type;
	if (!x || !y) return (x != NULL) - (y != NULL);
	return strcmp(x, y);
}

// One entry per name: the same type's name has an address in each image (a program and its scripts).
static int32_t merge_names(c4_gc_census_entry* entries, int32_t count)
{
	qsort(entries, (size_t)count, sizeof(c4_gc_census_entry), by_name);
	int32_t kept = 0;
	for (int32_t i = 0; i < count; ++i)
	{
		if (kept > 0 && by_name(&entries[kept - 1], &entries[i]) == 0)
		{
			entries[kept - 1].objects += entries[i].objects;
			entries[kept - 1].bytes += entries[i].bytes;
			entries[kept - 1].nativeBytes += entries[i].nativeBytes;
		}
		else entries[kept++] = entries[i];
	}
	return kept;
}

static void forget_span(const GCSpan* span, uint32_t epoch, uintptr_t low, uintptr_t high)
{
	if (!span->tags || span->tagsEpoch != epoch) return;
	for (uint32_t slot = 0; slot < span->nelems; ++slot)
	{
		const uintptr_t record = (uintptr_t)span->tags[slot];
		if (record >= low && record < high) span->tags[slot] = NULL;
	}
}

void gc_census_forget_in(const GCCensus* census, const GCHeap* heap, const void* low, const void* high)
{
	const uint32_t epoch = census->epoch;
	for (uint32_t c = 0; c < GC_SPAN_CLASSES; ++c)
		for (const GCSpan* span = heap->classes[c].all; span; span = span->next) forget_span(span, epoch, (uintptr_t)low, (uintptr_t)high);
	for (const GCSpan* span = heap->large; span; span = span->next) forget_span(span, epoch, (uintptr_t)low, (uintptr_t)high);
}

void gc_census_take(GCCensus* census, const GCHeap* heap)
{
	Tally tally = { census->last, NULL, 0, census->capacity > 0 ? census->capacity : 64, NULL, 0 };
	if (!tally.entries) tally.entries = (c4_gc_census_entry*)gc_mem_alloc((size_t)tally.capacity * sizeof(c4_gc_census_entry));
	memset(tally.entries, 0, (size_t)tally.capacity * sizeof(c4_gc_census_entry));
	tally.types = (const c4_gc_type**)gc_mem_alloc((size_t)tally.capacity * sizeof(const c4_gc_type*));
	rehash(&tally, 256u);
	const uint32_t epoch = census->epoch;
	for (uint32_t c = 0; c < GC_SPAN_CLASSES; ++c)
		for (const GCSpan* span = heap->classes[c].all; span; span = span->next) count_span(&tally, span, epoch);
	for (const GCSpan* span = heap->large; span; span = span->next) count_span(&tally, span, epoch);
	gc_mem_free(tally.slots);
	gc_mem_free((void*)tally.types);
	census->last = tally.entries;
	census->capacity = tally.capacity;
	census->count = merge_names(tally.entries, tally.count);
}

int32_t gc_census_last(const GCCensus* census, c4_gc_census_entry* out, int32_t capacity)
{
	const int32_t copied = out && capacity > 0 ? (capacity < census->count ? capacity : census->count) : 0;
	if (copied) memcpy(out, census->last, (size_t)copied * sizeof(c4_gc_census_entry));
	return census->count;
}
