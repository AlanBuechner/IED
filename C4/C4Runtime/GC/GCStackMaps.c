#include "GC/GCStackMaps.h"
#include <string.h>

// LLVM's stack map, version 3 (llvm.org/docs/StackMaps.html): a header, the functions with their record counts, the
// large constants, then the records - each a call's return address (the function's address plus its offset) and its
// locations; a statepoint's are its calling convention, its flags and its count of deopt operands, then the deopt
// operands (Compiler/StackMaps.cpp): the count of words of the values live across the call that may hold an address, those
// words (each where it is while the call runs: a register, a spill slot), then each stack slot that may hold a pointer,
// its word count and a mask per 32 words.
enum { kVersion = 3, kHeaderBytes = 16, kFunctionBytes = 24, kRecordHeaderBytes = 16, kLocationBytes = 12 };
enum { kRegister = 1, kDirect = 2, kIndirect = 3, kConstant = 4, kConstantIndex = 5 };
enum { kStatepointHeader = 3, kDeoptCount = 2 };
enum { kWordBytes = 8, kMaskWords = 32 };
#define GC_SLOT_WHOLE 0x80000000u

struct GCStackRecord
{
	uintptr_t pc;
	const uint8_t* record;
	const uint8_t* constants; // its stack map's large constants
	uint32_t constantCount;
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

static uint16_t read16(const uint8_t* at) { uint16_t v; memcpy(&v, at, sizeof v); return v; }
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

// One object's stack map at `at`: its records added; where the next one starts, or NULL when it is not one.
static const uint8_t* add_map(const uint8_t* at, const uint8_t* end)
{
	if (end - at < kHeaderBytes || at[0] != kVersion) return NULL;
	const uint32_t functions = read32(at + 4);
	const uint32_t constants = read32(at + 8);
	const uint8_t* function = at + kHeaderBytes;
	const uint8_t* constant = function + (size_t)functions * kFunctionBytes;
	const uint8_t* record = constant + (size_t)constants * kWordBytes;
	if (record > end) return NULL;
	for (uint32_t f = 0; f < functions; ++f, function += kFunctionBytes)
	{
		const uint64_t address = read64(function);
		const uint64_t count = read64(function + 16);
		for (uint64_t r = 0; r < count; ++r)
		{
			if (end - record < kRecordHeaderBytes) return NULL;
			const uint16_t locations = read16(record + 14);
			const uint8_t* liveOuts = align8(record + kRecordHeaderBytes + (size_t)locations * kLocationBytes);
			if (end - liveOuts < 4) return NULL;
			const uint8_t* next = align8(liveOuts + 4 + (size_t)read16(liveOuts + 2) * 4);
			if (next > end) return NULL;
			const GCStackRecord entry = { (uintptr_t)(address + read32(record + 8)), record, constant, constants };
			add_record(&entry);
			record = next;
		}
	}
	return record;
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

static uint32_t constant_of(const GCStackRecord* record, const uint8_t* location)
{
	const uint8_t kind = location[0];
	const uint32_t small = read32(location + 8);
	if (kind == kConstant) return small;
	if (kind == kConstantIndex && small < record->constantCount) return (uint32_t)read64(record->constants + (size_t)small * kWordBytes);
	return 0;
}

// Where a listed stack slot starts: a static slot's address (Direct), or one kept in a register or a spill slot.
static int slot_address(const uint8_t* location, const GCFrameState* frame, uintptr_t* address)
{
	uint64_t base = 0;
	if (!register_of(frame, read16(location + 4), &base)) return 0;
	const int32_t offset = (int32_t)read32(location + 8);
	switch (location[0])
	{
	case kDirect: *address = (uintptr_t)(base + (int64_t)offset); return 1;
	case kIndirect: *address = (uintptr_t)read64((const uint8_t*)(uintptr_t)(base + (int64_t)offset)); return 1;
	case kRegister: *address = (uintptr_t)base; return 1;
	default: return 0;
	}
}

// One word a record names: in a register, or in a stack slot (Indirect); a stack slot's own address (Direct) and a
// constant name no object.
static int scan_value(const uint8_t* location, const GCFrameState* frame, gc_scan_range_fn now, void* ctx)
{
	uint64_t value = 0;
	if (location[0] == kIndirect)
	{
		if (!register_of(frame, read16(location + 4), &value)) return 0;
		const uintptr_t slot = (uintptr_t)(value + (int64_t)(int32_t)read32(location + 8));
		now(ctx, (const void*)slot, (const void*)(slot + kWordBytes));
	}
	else if (location[0] == kRegister)
	{
		if (!register_of(frame, read16(location + 4), &value)) return 0;
		now(ctx, &value, &value + 1);
	}
	return 1;
}

int gc_stack_record_scan(const GCStackRecord* record, const GCFrameState* frame, gc_scan_range_fn now, gc_scan_range_fn later, void* ctx)
{
	const uint8_t* locations = record->record + kRecordHeaderBytes;
	const uint16_t count = read16(record->record + 14);
	if (count < kStatepointHeader) return 1; // a record of no statepoint: nothing lives across the call
	const size_t deoptEnd = kStatepointHeader + (size_t)constant_of(record, locations + kDeoptCount * kLocationBytes);
	if (deoptEnd > count || deoptEnd == kStatepointHeader) return 0;
	const size_t valuesEnd = kStatepointHeader + 1 + (size_t)constant_of(record, locations + kStatepointHeader * kLocationBytes);
	if (valuesEnd > deoptEnd) return 0;
	size_t i = kStatepointHeader + 1;
	for (; i < valuesEnd; ++i)
		if (!scan_value(locations + i * kLocationBytes, frame, now, ctx)) return 0;
	while (i < deoptEnd)
	{
		uintptr_t base = 0;
		if (i + 1 >= deoptEnd || !slot_address(locations + i * kLocationBytes, frame, &base)) return 0;
		const uint32_t words = constant_of(record, locations + (i + 1) * kLocationBytes);
		i += 2;
		if (words & GC_SLOT_WHOLE)
		{
			later(ctx, (const void*)base, (const void*)(base + (uintptr_t)(words & ~GC_SLOT_WHOLE) * kWordBytes));
			continue;
		}
		for (uint32_t first = 0; first < words; first += kMaskWords, ++i)
		{
			if (i >= deoptEnd) return 0;
			for (uint32_t mask = constant_of(record, locations + i * kLocationBytes); mask; mask &= mask - 1u)
			{
				const uintptr_t word = base + (uintptr_t)(first + (uint32_t)__builtin_ctz(mask)) * kWordBytes;
				now(ctx, (const void*)word, (const void*)(word + kWordBytes));
			}
		}
	}
	return 1;
}
