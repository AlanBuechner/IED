#include "GC/GCCensus.h"
#include "GC/GCPlatform.h"
#include <stdlib.h>
#include <string.h>

// The entries of a census being taken, found by the address of their type's name (open addressing).
typedef struct Tally
{
	c4_gc_census_entry* entries;
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

void gc_census_note(const GCCensus* census, const GCHeap* heap, void* p, const char* type)
{
	GCSpan* span = gc_pages_span_of(&heap->pages, (uintptr_t)p);
	if (!span || span->state == GC_SPAN_POOL) return;
	const uint32_t epoch = __atomic_load_n(&census->epoch, __ATOMIC_ACQUIRE);
	if (!span->tags) span->tags = (const char**)gc_mem_alloc(span->nelems * sizeof(const char*));
	if (!span->tags) return;
	if (span->tagsEpoch != epoch)
	{
		memset((void*)span->tags, 0, span->nelems * sizeof(const char*));
		span->tagsEpoch = epoch;
	}
	span->tags[gc_span_slot(span, (size_t)((uint8_t*)p - span->base))] = type;
}

static uint32_t slot_of(const char* type, uint32_t mask)
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
		uint32_t s = slot_of(tally->entries[i].type, tally->slotMask);
		while (tally->slots[s]) s = (s + 1u) & tally->slotMask;
		tally->slots[s] = i + 1;
	}
}

// The entry of `type`, added when it is new.
static c4_gc_census_entry* entry_of(Tally* tally, const char* type)
{
	uint32_t s = slot_of(type, tally->slotMask);
	for (; tally->slots[s]; s = (s + 1u) & tally->slotMask)
		if (tally->entries[tally->slots[s] - 1].type == type) return &tally->entries[tally->slots[s] - 1];
	if (tally->count == tally->capacity)
	{
		tally->capacity *= 2;
		tally->entries = (c4_gc_census_entry*)gc_mem_realloc(tally->entries, (size_t)tally->capacity * sizeof(c4_gc_census_entry));
	}
	c4_gc_census_entry* entry = &tally->entries[tally->count];
	entry->type = type;
	tally->slots[s] = ++tally->count;
	if ((uint32_t)tally->count * 2u > tally->slotMask) rehash(tally, (tally->slotMask + 1u) * 2u);
	return entry;
}

static void count_span(Tally* tally, const GCSpan* span, uint32_t epoch)
{
	const char* const* tags = span->tags && span->tagsEpoch == epoch ? span->tags : NULL;
	c4_gc_census_entry* entry = NULL;
	for (uint32_t w = 0; w < GC_SPAN_WORDS; ++w)
		for (uint64_t bits = span->allocBits[w]; bits; bits &= bits - 1u)
		{
			const char* type = tags ? tags[w * 64u + (uint32_t)__builtin_ctzll(bits)] : NULL;
			if (!entry || entry->type != type) entry = entry_of(tally, type);
			entry->objects += 1;
			entry->bytes += span->elemSize;
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
		}
		else entries[kept++] = entries[i];
	}
	return kept;
}

void gc_census_take(GCCensus* census, const GCHeap* heap)
{
	Tally tally = { census->last, 0, census->capacity > 0 ? census->capacity : 64, NULL, 0 };
	if (!tally.entries) tally.entries = (c4_gc_census_entry*)gc_mem_alloc((size_t)tally.capacity * sizeof(c4_gc_census_entry));
	memset(tally.entries, 0, (size_t)tally.capacity * sizeof(c4_gc_census_entry));
	rehash(&tally, 256u);
	const uint32_t epoch = census->epoch;
	for (uint32_t c = 0; c < GC_SPAN_CLASSES; ++c)
		for (const GCSpan* span = heap->classes[c].all; span; span = span->next) count_span(&tally, span, epoch);
	for (const GCSpan* span = heap->large; span; span = span->next) count_span(&tally, span, epoch);
	gc_mem_free(tally.slots);
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
