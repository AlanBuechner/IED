#include "GC/GCStackMaps.h"
#include <string.h>

// C4's stack maps (Compiler/StackMapFormat.h, which says the format): one an object, each a header, its functions'
// addresses, then function by function its slots and its records. A record is a call's return address (the function's
// address plus its offset), the values live across the call that may hold an address (each where it is while the call
// runs: a register, a word in memory), and a bit per slot of its function for those it lists - each a stack slot that
// may hold a pointer, with its word count and a mask per 32 words.
enum { kMagic = 0x4D533443, kVersion = 1, kHeaderBytes = 16, kAddressBytes = 8, kOffsetBytes = 4 };
enum { kSlotDirect = 0, kSlotIndirect = 1, kSlotRegister = 2 };
enum { kWordBytes = 8, kMaskWords = 32 };

struct GCStackRecord
{
	uintptr_t pc;
	const uint8_t* record;
	const uint8_t* slots; // its function's slots
	uint32_t slotCount;
};

typedef struct StackMapTable
{
	GCStackRecord* slots; // open addressing by pc; pc 0 is empty
	size_t capacity;
	size_t count;
	int loaded;
} StackMapTable;

static StackMapTable g_maps;
static _Thread_local uintptr_t t_collectionCaller;

void gc_stack_set_collection_caller(uintptr_t returnAddress) { t_collectionCaller = returnAddress; }
uintptr_t gc_stack_collection_caller(void) { return t_collectionCaller; }

static uint32_t read32(const uint8_t* at) { uint32_t v; memcpy(&v, at, sizeof v); return v; }
static uint64_t read64(const uint8_t* at) { uint64_t v; memcpy(&v, at, sizeof v); return v; }
static const uint8_t* align8(const uint8_t* at) { return (const uint8_t*)(((uintptr_t)at + 7u) & ~(uintptr_t)7u); }

static size_t slot_of(uintptr_t pc, size_t capacity) { return (size_t)((pc * 0x9E3779B97F4A7C15ull) >> 17) & (capacity - 1); }

static void insert(GCStackRecord* slots, size_t capacity, const GCStackRecord* record)
{
	size_t i = slot_of(record->pc, capacity);
	while (slots[i].pc && slots[i].pc != record->pc) i = (i + 1) & (capacity - 1);
	slots[i] = *record;
}

static void grow(void)
{
	const size_t capacity = g_maps.capacity ? g_maps.capacity * 2 : 1024;
	GCStackRecord* slots = (GCStackRecord*)gc_mem_alloc(capacity * sizeof(GCStackRecord));
	if (!slots) gc_os_fatal("out of memory while reading the stack maps");
	for (size_t i = 0; i < g_maps.capacity; ++i)
		if (g_maps.slots[i].pc) insert(slots, capacity, &g_maps.slots[i]);
	gc_mem_free(g_maps.slots);
	g_maps.slots = slots;
	g_maps.capacity = capacity;
}

static void add_record(const GCStackRecord* record)
{
	if ((g_maps.count + 1) * 2 > g_maps.capacity) grow();
	insert(g_maps.slots, g_maps.capacity, record);
	++g_maps.count;
}

// LEB128 numbers. Those of a map are checked as it is added (a number past `end` fails it: *at becomes NULL), so a
// record is read again without checks.
static uint64_t uleb_within(const uint8_t** at, const uint8_t* end)
{
	uint64_t value = 0;
	for (unsigned shift = 0; *at && *at < end && shift < 64; shift += 7)
	{
		const uint8_t byte = *(*at)++;
		value |= (uint64_t)(byte & 0x7f) << shift;
		if (!(byte & 0x80)) return value;
	}
	*at = NULL;
	return 0;
}

static void sleb_skip(const uint8_t** at, const uint8_t* end)
{
	while (*at && *at < end)
		if (!(*(*at)++ & 0x80)) return;
	*at = NULL;
}

static uint64_t uleb(const uint8_t** at)
{
	uint64_t value = 0;
	for (unsigned shift = 0;; shift += 7)
	{
		const uint8_t byte = *(*at)++;
		value |= (uint64_t)(byte & 0x7f) << shift;
		if (!(byte & 0x80)) return value;
	}
}

static int64_t sleb(const uint8_t** at)
{
	int64_t value = 0;
	unsigned shift = 0;
	uint8_t byte;
	do
	{
		byte = *(*at)++;
		value |= (int64_t)(byte & 0x7f) << shift;
		shift += 7;
	} while (byte & 0x80);
	if (shift < 64 && (byte & 0x40)) value |= -((int64_t)1 << shift);
	return value;
}

// A slot of a function's map (see GCSlot below): where the next one starts, NULL when it does not fit before `end`.
static const uint8_t* skip_slot(const uint8_t* at, const uint8_t* end)
{
	uleb_within(&at, end);
	sleb_skip(&at, end);
	const uint64_t words = uleb_within(&at, end);
	if (!(words & 1))
		for (uint64_t mask = 0; at && mask < ((words >> 1) + kMaskWords - 1) / kMaskWords; ++mask) uleb_within(&at, end);
	return at;
}

// A record whose return offset is at `at`: where the next one starts, NULL when it does not fit before `end`.
static const uint8_t* skip_record(const uint8_t* at, const uint8_t* end, uint32_t slotCount)
{
	if (end - at < kOffsetBytes) return NULL;
	at += kOffsetBytes;
	const uint64_t values = uleb_within(&at, end);
	if (values & 1) return at; // the frame is read whole: nothing follows
	for (uint64_t v = 0; at && v < (values >> 1); ++v)
		if (uleb_within(&at, end) & 1) sleb_skip(&at, end);
	if (!at || (size_t)(end - at) < ((size_t)slotCount + 7) / 8) return NULL;
	return at + ((size_t)slotCount + 7) / 8;
}

// One object's stack map at `at`: its records added; where the next one starts, or NULL when it is not one.
static const uint8_t* add_map(const uint8_t* at, const uint8_t* end)
{
	if (end - at < kHeaderBytes || read32(at) != kMagic || read32(at + 4) != kVersion) return NULL;
	const uint32_t functions = read32(at + 8);
	const uint8_t* mapEnd = at + read32(at + 12);
	const uint8_t* address = at + kHeaderBytes;
	const uint8_t* stream = address + (size_t)functions * kAddressBytes;
	if (mapEnd > end || stream > mapEnd) return NULL;
	for (uint32_t f = 0; f < functions; ++f, address += kAddressBytes)
	{
		const uint64_t function = read64(address);
		const uint64_t records = uleb_within(&stream, mapEnd);
		const uint64_t slotCount = uleb_within(&stream, mapEnd);
		const uint8_t* slots = stream;
		for (uint64_t s = 0; stream && s < slotCount; ++s) stream = skip_slot(stream, mapEnd);
		if (!stream || slotCount > UINT32_MAX) return NULL;
		for (uint64_t r = 0; r < records; ++r)
		{
			const uint8_t* next = skip_record(stream, mapEnd, (uint32_t)slotCount);
			if (!next) return NULL;
			const GCStackRecord entry = { (uintptr_t)(function + read32(stream)), stream, slots, (uint32_t)slotCount };
			add_record(&entry);
			stream = next;
		}
	}
	return mapEnd;
}

void gc_stack_maps_add(const uint8_t* start, const uint8_t* end)
{
	const uint8_t* at = align8(start);
	while (at && end - at >= kHeaderBytes)
	{
		if (read64(at) == 0) at += kWordBytes; // the alignment between two objects' maps
		else at = add_map(at, end);
	}
}

int gc_stack_maps_present(void)
{
	if (!g_maps.loaded)
	{
		g_maps.loaded = 1;
		const uint8_t* start = NULL;
		const uint8_t* end = NULL;
		gc_os_stack_maps(&start, &end);
		if (start && end > start) gc_stack_maps_add(start, end);
	}
	return g_maps.count != 0;
}

const GCStackRecord* gc_stack_maps_find(uintptr_t pc)
{
	if (!g_maps.count || !pc) return NULL;
	for (size_t i = slot_of(pc, g_maps.capacity); g_maps.slots[i].pc; i = (i + 1) & (g_maps.capacity - 1))
		if (g_maps.slots[i].pc == pc) return &g_maps.slots[i];
	return NULL;
}

// --- reading a frame whole ---------------------------------------------------------------------------------------------

void gc_save_slots_add(GCSaveSlots* saves, uintptr_t at, unsigned bytes)
{
	if (!at || saves->count == GC_SAVES_MOST) return; // one not skipped is read: it keeps a value alive, no more
	saves->at[saves->count] = at;
	saves->bytes[saves->count] = (uint8_t)bytes;
	saves->count++;
}

void gc_scan_frame_whole(uintptr_t from, uintptr_t to, GCSaveSlots* saves, gc_scan_range_fn later, void* ctx)
{
	for (unsigned i = 1; i < saves->count; ++i) // by address
		for (unsigned j = i; j > 0 && saves->at[j - 1] > saves->at[j]; --j)
		{
			const uintptr_t at = saves->at[j];
			const uint8_t bytes = saves->bytes[j];
			saves->at[j] = saves->at[j - 1];
			saves->bytes[j] = saves->bytes[j - 1];
			saves->at[j - 1] = at;
			saves->bytes[j - 1] = bytes;
		}
	uintptr_t cursor = from;
	for (unsigned i = 0; i < saves->count && cursor < to; ++i)
	{
		const uintptr_t start = saves->at[i], end = saves->at[i] + saves->bytes[i];
		if (end <= cursor || start >= to) continue;
		if (start > cursor) later(ctx, (const void*)cursor, (const void*)start);
		cursor = end;
	}
	if (cursor < to) later(ctx, (const void*)cursor, (const void*)to);
}

void gc_arguments_after_whole(GCArgumentBytes* owed, uintptr_t callerSp, uintptr_t top)
{
	uintptr_t to = callerSp + GC_FRAME_ARGUMENT_BYTES;
	if (owed->to > to) to = owed->to;
	owed->from = callerSp;
	owed->to = to < top ? to : top;
}

void gc_arguments_in_mapped(GCArgumentBytes* owed, uintptr_t sp, uintptr_t callerSp, GCSaveSlots* saves, gc_scan_range_fn later, void* ctx)
{
	if (owed->from >= owed->to) return;
	const uintptr_t from = owed->from > sp ? owed->from : sp;
	const uintptr_t to = owed->to < callerSp ? owed->to : callerSp;
	if (from < to) gc_scan_frame_whole(from, to, saves, later, ctx);
	owed->from = callerSp;
}

// --- reading a frame by its record -------------------------------------------------------------------------------

static int register_of(const GCFrameState* frame, uint16_t number, uint64_t* value)
{
	if (number >= GC_FRAME_REGISTERS || !(frame->known & (1u << number))) return 0;
	*value = frame->reg[number];
	return 1;
}

// One word a record names: in a register, or in memory (a spill slot, an incoming argument's slot).
static int scan_value(const uint8_t** at, const GCFrameState* frame, gc_scan_range_fn now, void* ctx)
{
	const uint64_t place = uleb(at);
	uint64_t value = 0;
	if (!register_of(frame, (uint16_t)(place >> 1), &value)) return 0;
	if (place & 1)
	{
		const uintptr_t word = (uintptr_t)(value + (uint64_t)sleb(at));
		now(ctx, (const void*)word, (const void*)(word + kWordBytes));
	}
	else now(ctx, &value, &value + 1);
	return 1;
}

// A stack slot a function's records may list: where it starts, its words, whether each is read, and for each 32 words
// a mask of those that may hold a pointer.
typedef struct GCSlot
{
	uint8_t base;
	uint16_t reg;
	int64_t offset;
	uint64_t words;
	int whole;
	const uint8_t* masks;
} GCSlot;

static GCSlot read_slot(const uint8_t** at)
{
	GCSlot slot;
	const uint64_t place = uleb(at);
	slot.base = (uint8_t)(place & 3);
	slot.reg = (uint16_t)(place >> 2);
	slot.offset = sleb(at);
	const uint64_t words = uleb(at);
	slot.words = words >> 1;
	slot.whole = (int)(words & 1);
	slot.masks = *at;
	if (!slot.whole)
		for (uint64_t mask = 0; mask < (slot.words + kMaskWords - 1) / kMaskWords; ++mask) uleb(at);
	return slot;
}

// Where `slot` starts in the frame whose registers are `frame`.
static int slot_address(const GCSlot* slot, const GCFrameState* frame, uintptr_t* address)
{
	uint64_t base = 0;
	if (!register_of(frame, slot->reg, &base)) return 0;
	switch (slot->base)
	{
	case kSlotDirect: *address = (uintptr_t)(base + (uint64_t)slot->offset); return 1;
	case kSlotIndirect: *address = (uintptr_t)read64((const uint8_t*)(uintptr_t)(base + (uint64_t)slot->offset)); return 1;
	case kSlotRegister: *address = (uintptr_t)base; return 1;
	default: return 0;
	}
}

static int scan_slot(const GCSlot* slot, const GCFrameState* frame, gc_scan_range_fn now, gc_scan_range_fn later, void* ctx)
{
	uintptr_t base = 0;
	if (!slot_address(slot, frame, &base)) return 0;
	if (slot->whole)
	{
		later(ctx, (const void*)base, (const void*)(base + (uintptr_t)slot->words * kWordBytes));
		return 1;
	}
	const uint8_t* masks = slot->masks;
	for (uint64_t first = 0; first < slot->words; first += kMaskWords)
		for (uint64_t mask = uleb(&masks); mask; mask &= mask - 1u)
		{
			const uintptr_t word = base + (uintptr_t)(first + (uint64_t)__builtin_ctzll(mask)) * kWordBytes;
			now(ctx, (const void*)word, (const void*)(word + kWordBytes));
		}
	return 1;
}

int gc_stack_record_scan(const GCStackRecord* record, const GCFrameState* frame, gc_scan_range_fn now, gc_scan_range_fn later, void* ctx)
{
	const uint8_t* at = record->record + kOffsetBytes;
	const uint64_t values = uleb(&at);
	if (values & 1) return 0; // the record says to read the frame whole
	for (uint64_t v = 0; v < (values >> 1); ++v)
		if (!scan_value(&at, frame, now, ctx)) return 0;
	const uint8_t* listed = at;
	const uint8_t* slots = record->slots;
	for (uint32_t s = 0; s < record->slotCount; ++s)
	{
		const GCSlot slot = read_slot(&slots);
		if ((listed[s / 8] & (1u << (s % 8))) && !scan_slot(&slot, frame, now, later, ctx)) return 0;
	}
	return 1;
}
