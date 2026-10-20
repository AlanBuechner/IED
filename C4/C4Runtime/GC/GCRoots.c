#include "GC/GCCollector.h"
#include "GC/GCPlatform.h"
#include <stdlib.h>

static void mark_globals(GCMarker* marker, const c4_gc_root_range* globals)
{
	for (const c4_gc_root_range* r = globals; r && r->start; ++r)
		gc_mark_range_later(marker, r->start, (const uint8_t*)r->start + r->size);
}

static void mark_root_tables(GCMarker* marker, const GCPtrMap* tables)
{
	for (size_t i = 0; i < tables->capacity; ++i)
		if (tables->slots[i].key) mark_globals(marker, (const c4_gc_root_range*)tables->slots[i].key);
}

static void mark_regions(GCMarker* marker, const GCPtrMap* regions)
{
	for (size_t i = 0; i < regions->capacity; ++i)
	{
		const GCEntry* region = &regions->slots[i];
		if (region->key) gc_mark_range_later(marker, region->key, (const uint8_t*)region->key + (size_t)region->a);
	}
}

static void mark_pins(GCMarker* marker, const GCPtrMap* pins)
{
	for (size_t i = 0; i < pins->capacity; ++i)
	{
		if (!pins->slots[i].key) continue;
		GCObject object = gc_heap_find(marker->heap, pins->slots[i].key);
		if (object.span) gc_mark_object(marker, object);
	}
}

static void visit_root(void* marker, void* ptr)
{
	gc_mark_word((GCMarker*)marker, (uintptr_t)ptr);
}

static void mark_enumerated(GCMarker* marker, const GCPtrMap* enumerators)
{
	// An enumerator may register or unregister enumerators: run a copy, skipping entries removed meanwhile.
	const size_t count = enumerators->count;
	if (!count) return;
	GCEntry* list = gc_map_snapshot(enumerators);
	for (size_t i = 0; i < count; ++i)
	{
		const GCEntry* current = gc_map_find(enumerators, list[i].key);
		if (current) ((c4_gc_enum_roots_fn)current->a)(current->b, visit_root, marker);
	}
	gc_mem_free(list);
}

// The enumerators run first: they are program code, and may change the regions that are queued after them.
// The collecting thread's own stack and registers are scanned now; the other ranges wait for the drain.
void gc_mark_roots(GCMarker* marker, const GCState* gc, const GCThread* self)
{
	mark_enumerated(marker, &gc->enumerators);
	gc_os_scan_stack(gc_mark_range_callback, marker);
	mark_globals(marker, gc->globals);
	mark_root_tables(marker, &gc->rootTables);
	gc_threads_mark(marker, &gc->threads, self);
	mark_regions(marker, &gc->regions);
	mark_pins(marker, &gc->pins);
	gc_finalize_mark_queue(marker, &gc->ready);
}

void gc_pin(GCState* gc, void* ptr)
{
	GCObject object = gc_heap_find(&gc->heap, ptr);
	if (!object.span) return;
	GCEntry* pin = gc_map_upsert(&gc->pins, gc_object_base(object));
	pin->a = (void*)((uintptr_t)pin->a + 1);
}

void gc_unpin(GCState* gc, void* ptr)
{
	GCObject object = gc_heap_find(&gc->heap, ptr);
	if (!object.span) return;
	const void* base = gc_object_base(object);
	GCEntry* pin = gc_map_find(&gc->pins, base);
	if (!pin) return;
	if ((uintptr_t)pin->a > 1) pin->a = (void*)((uintptr_t)pin->a - 1);
	else gc_map_remove(&gc->pins, base);
}
