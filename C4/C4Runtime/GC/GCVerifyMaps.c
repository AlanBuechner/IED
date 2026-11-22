#include "GC/GCVerifyMaps.h"
#include "GC/GCCollector.h"
#include <stdio.h>
#if defined(_WIN32)
#include <process.h>
static unsigned long long current_pid(void) { return (unsigned long long)_getpid(); }
#else
#include <unistd.h>
static unsigned long long current_pid(void) { return (unsigned long long)getpid(); }
#endif

// As the marker reads a word (GCMark.c): a NaN with its top 13 bits set holds a pointer in its low 47 bits (D-85).
#define GC_BOXED_WORDS 0xFFF8000000000000ull
#define GC_BOXED_POINTER ((1ull << 47) - 1)

typedef struct Holder
{
	GCMapHolder kind;
	const void* key;
	const char* type;
	const void* where;
} Holder;

static int map_bit(const uint64_t* bits, size_t word) { return (int)((bits[word >> 6] >> (word & 63u)) & 1u); }

static void note(GCMapReports* reports, Holder holder, uint32_t word, uintptr_t value, int unmarked)
{
	reports->misses++;
	for (uint32_t i = 0; i < reports->count; ++i)
	{
		GCMapReport* known = &reports->found[i];
		if (known->key != holder.key || known->word != word) continue;
		if (unmarked && !known->unmarked)
		{
			known->unmarked = 1;
			if (i < reports->printed) reports->printed = i;   // printed again, now that it would free a live target
		}
		return;
	}
	if (reports->count == GC_MAP_REPORTS) return;
	GCMapReport* report = &reports->found[reports->count++];
	report->holder = holder.kind;
	report->key = holder.key;
	report->type = holder.type;
	report->where = holder.where;
	report->word = word;
	report->unmarked = unmarked;
	report->value = value;
}

// A word a map says holds no pointer: reported when it names an object of the heap.
static void check(GCState* gc, Holder holder, uint32_t word, uintptr_t raw)
{
	const uintptr_t value = raw >= GC_BOXED_WORDS ? raw & GC_BOXED_POINTER : raw;
	if (value - gc->heap.pages.lo >= gc->heap.pages.hi - gc->heap.pages.lo) return;
	const GCObject target = gc_heap_find(&gc->heap, (const void*)value);
	if (!target.span || target.span->state == GC_SPAN_POOL) return;
	note(&gc->mapReports, holder, word, value, !gc_object_marked(target));
}

// The name of the type whose map `map` is, when a record of it is known.
static const char* name_of_map(const GCPointerMaps* maps, const GCSpanMap* map)
{
	for (size_t i = 0; i < maps->byRecord.capacity; ++i)
	{
		const GCEntry* entry = &maps->byRecord.slots[i];
		if (entry->key && entry->a == (void*)map) return ((const c4_gc_type*)entry->key)->name;
	}
	return NULL;
}

// The words of one object (an array's elements to the slot's end) its map leaves out; an array's count is a number.
static void check_object(GCState* gc, Holder holder, const GCSpanMap* map, const uint8_t* base, size_t bytes)
{
	const uintptr_t* words = (const uintptr_t*)base;
	const size_t count = bytes >> 3;
	if (!map->array)
	{
		for (size_t w = 0; w < count && w < map->words; ++w)
			if (!map_bit(map->bits, w)) check(gc, holder, (uint32_t)w, words[w]);
		return;
	}
	for (size_t w = map->firstWords; w < count; ++w)
	{
		const size_t e = (w - map->firstWords) % map->words;
		if (!map_bit(map->bits, e)) check(gc, holder, (uint32_t)e, words[w]);
	}
}

static void check_span(GCState* gc, const GCSpan* span)
{
	if (!span->map) return;
	const char* type = span->typeKey ? ((const c4_gc_type*)gc_typed_key_record(span->typeKey))->name : name_of_map(&gc->heap.maps, span->map);
	for (uint32_t slot = 0; slot < span->nelems; ++slot)
	{
		if (!gc_bit(span->allocBits, slot) || !gc_bit(span->markBits, slot)) continue;
		const uint8_t* base = gc_span_slot_base(span, slot);
		const Holder holder = { GC_MAP_OBJECT, span->map, type, base };
		check_object(gc, holder, span->map, base, span->elemSize);
	}
}

static void check_objects(GCState* gc)
{
	for (uint32_t c = GC_SCAN_PRECISE; c < GC_SPAN_CLASSES; c += GC_SCAN_KINDS)
		for (const GCSpan* span = gc->heap.classes[c].all; span; span = span->next)
			if (span->state == GC_SPAN_SMALL) check_span(gc, span);
	for (const GCSpan* span = gc->heap.large; span; span = span->next)
		if (span->scanKind == GC_SCAN_PRECISE) check_span(gc, span);
}

// Each element of a typed region, as gc_mark_typed_region reads it.
static void check_regions(GCState* gc)
{
	for (size_t i = 0; i < gc->regions.capacity; ++i)
	{
		const GCEntry* region = &gc->regions.slots[i];
		const GCSpanMap* map = (const GCSpanMap*)region->b;
		if (!region->key || !map) continue;
		const Holder holder = { GC_MAP_REGION, map, name_of_map(&gc->heap.maps, map), region->key };
		const uintptr_t* words = (const uintptr_t*)region->key;
		const size_t count = (size_t)region->a >> 3;
		for (size_t w = 0; w < count; ++w)
		{
			const size_t e = w % map->words;
			if (!map_bit(map->bits, e)) check(gc, holder, (uint32_t)e, words[w]);
		}
	}
}

static void check_range(GCState* gc, GCMapHolder kind, const c4_gc_root_range* range)
{
	if (!range->pointers || ((uintptr_t)range->start & 7u)) return;   // read whole
	const Holder holder = { kind, kind == GC_MAP_GLOBAL ? (const void*)range : (const void*)range->pointers, NULL, range->start };
	const uintptr_t* words = (const uintptr_t*)range->start;
	for (size_t w = 0; w < (size_t)range->size >> 3; ++w)
		if (!map_bit(range->pointers, w)) check(gc, holder, (uint32_t)w, words[w]);
}

static void check_globals(GCState* gc, const c4_gc_root_range* globals)
{
	for (const c4_gc_root_range* range = globals; range && range->start; ++range) check_range(gc, GC_MAP_GLOBAL, range);
}

static void check_roots(GCState* gc)
{
	check_globals(gc, gc->globals);
	for (size_t i = 0; i < gc->rootTables.capacity; ++i)
		if (gc->rootTables.slots[i].key) check_globals(gc, (const c4_gc_root_range*)gc->rootTables.slots[i].key);
	for (const GCThread* thread = gc->threads.all; thread; thread = thread->next)
		for (size_t i = 0; i < thread->localCount; ++i) check_range(gc, GC_MAP_THREAD_LOCAL, &thread->locals[i]);
}

void gc_verify_maps(GCState* gc)
{
	gc->mapReports.checks++;
	check_objects(gc);
	check_regions(gc);
	check_roots(gc);
}

uint32_t gc_verify_maps_take(GCMapReports* reports, GCMapReport* out, uint32_t capacity)
{
	uint32_t taken = 0;
	for (; reports->printed < reports->count && taken < capacity; ++reports->printed) out[taken++] = reports->found[reports->printed];
	return taken;
}

static const char* holder_name(GCMapHolder holder)
{
	switch (holder)
	{
	case GC_MAP_OBJECT: return "an object";
	case GC_MAP_REGION: return "an element of a scan region";
	case GC_MAP_GLOBAL: return "a global";
	case GC_MAP_THREAD_LOCAL: return "a thread_local";
	}
	return "?";
}

static FILE* open_log(const char* log) { return log ? fopen(log, "a") : stderr; }

static void close_log(FILE* out)
{
	if (out && out != stderr) fclose(out);
}

void gc_verify_maps_print(const GCMapReport* report, const char* log)
{
	FILE* out = open_log(log);
	if (!out) return;
	fprintf(out, "[gc] map misses a pointer: word %u of %s of %s at %p holds 0x%llx, an object of the heap%s\n", report->word,
		holder_name(report->holder), report->type ? report->type : "a type of no known name", report->where,
		(unsigned long long)report->value, report->unmarked ? " - left UNMARKED, so the sweep frees it" : "");
	close_log(out);
}

void gc_verify_maps_announce(const char* log)
{
	FILE* out = open_log(log);
	if (!out) return;
	fprintf(out, "[gc] map check on in process %llu\n", current_pid());
	close_log(out);
}

void gc_verify_maps_summary(uint64_t misses, const char* log)
{
	FILE* out = open_log(log);
	if (!out) return;
	fprintf(out, "[gc] map check: %llu words named an object of the heap where a map says none is\n", (unsigned long long)misses);
	close_log(out);
}
