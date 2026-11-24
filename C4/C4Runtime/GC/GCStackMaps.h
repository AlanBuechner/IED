#pragma once
// Precise stack scanning (R16 phase 3, `-fstack-maps`, docs/runtime/01 "Stacks"): the stack maps of the program's C4 code
// (Compiler/StackMapFormat.h) name, for each call a C4 function makes, where the managed pointers live across it are (a
// register, a word in memory) and which of its stack slots may hold one (each slot, its word count, a mask per 32 words). A
// stopped thread's stack is walked frame by frame by its unwind data: a frame waiting in a call with a record is read
// at those places only; every other frame - C, C++, the runtime, a script, the innermost one, a call left without a
// record - is read whole, with its registers. A program built without stack maps has none, and every frame is read whole.

#include "GC/GCPlatform.h"
#include <stdint.h>

// A frame's registers as the unwinder knows them where the frame waits: DWARF numbers (0-15 the general registers, 16
// the return address), `known` a bit per register it knows.
enum { GC_FRAME_REGISTERS = 17, GC_REGISTER_SP = 7, GC_REGISTER_FP = 6 };
typedef struct GCFrameState
{
	uint64_t reg[GC_FRAME_REGISTERS];
	uint32_t known;
} GCFrameState;

typedef struct GCStackRecord GCStackRecord;

// Whether the program has stack maps: it finds them (in its image) the first time it is asked.
int gc_stack_maps_present(void);
// The record of the call whose return address is `pc`, or NULL.
const GCStackRecord* gc_stack_maps_find(uintptr_t pc);
// Reads the places `record` names in the frame whose registers are `frame`: each live value and each word a listed slot
// may hold a pointer in through `now`, a slot read whole through `later`. 0 when the record names a place the frame's
// registers do not give (the caller then reads the frame whole).
int gc_stack_record_scan(const GCStackRecord* record, const GCFrameState* frame, gc_scan_range_fn now, gc_scan_range_fn later, void* ctx);

// The stack maps of one image, as the linker gathered them: the objects' maps one after another. The platform gives them
// (gc_os_stack_maps); a test may add its own.
void gc_stack_maps_add(const uint8_t* start, const uint8_t* end);
// Where the program's image keeps its stack maps: [*start, *end), both NULL when it has none.
void gc_os_stack_maps(const uint8_t** start, const uint8_t** end);

// How many bytes past a whole-read frame's end, into its caller's frame, are read with it: where the frame's stack
// arguments are, and on Windows its register arguments' home.
enum { GC_FRAME_ARGUMENT_BYTES = 160 };

// The argument bytes frames read whole have left to the frames above them, [from, to): a frame above read whole reads
// them with its own memory; one read by its record reads only those within it, without the slots where it saved its
// callers' registers (a value there is read with the frame it belongs to - a C4 frame's dead one not at all).
typedef struct GCArgumentBytes
{
	uintptr_t from;
	uintptr_t to;
} GCArgumentBytes;

// Where a frame keeps registers of its callers (the unwinder restored them from there). A whole read of the frame skips
// them: each value is read with the frame it belongs to, or is dead (a frame read by its record keeps nothing live in a
// register across a call). A register's value belongs to the frame that saved it, which may have set it; a frame that did
// not save one left it as its caller had it. So a frame read whole reads the callee-saved registers it saved (`saved`,
// `savedVectors`: a bit per register, by the platform's numbering), the innermost one of a stopped thread its volatile
// ones too, and the walk's last frame every one left.
enum { GC_SAVES_MOST = 40 };
typedef struct GCSaveSlots
{
	uintptr_t at[GC_SAVES_MOST];
	uint8_t bytes[GC_SAVES_MOST];
	unsigned count;
	uint32_t saved;
	uint32_t savedVectors;
} GCSaveSlots;
void gc_save_slots_add(GCSaveSlots* saves, uintptr_t at, unsigned bytes);
// Reads [from, to) whole through `later`, but for the save slots.
void gc_scan_frame_whole(uintptr_t from, uintptr_t to, GCSaveSlots* saves, gc_scan_range_fn later, void* ctx);
// A frame that ends at `callerSp` was read whole: its argument bytes (and those an inner one left past it) are owed.
void gc_arguments_after_whole(GCArgumentBytes* owed, uintptr_t callerSp, uintptr_t top);
// A frame [sp, callerSp) was read by its record: the argument bytes owed within it are read, but its save slots.
void gc_arguments_in_mapped(GCArgumentBytes* owed, uintptr_t sp, uintptr_t callerSp, GCSaveSlots* saves, gc_scan_range_fn later, void* ctx);

// Reads a stopped thread's stack, whose innermost frame's registers are `registers` (Windows: a CONTEXT; Linux: the
// stopped context's mcontext_t), up to `top`: a frame whose record gc_stack_record_scan reads, each other one whole (from
// its stack pointer to its caller's, and GC_FRAME_ARGUMENT_BYTES on) with the registers it saved (GCSaveSlots), the
// innermost one - stopped anywhere, on Linux maybe with data in the red zone below its stack pointer - with its volatile
// registers too; where the unwind data ends or makes no sense, the rest of the stack whole with every register left. The
// platform's walker (GCStackWalkWin32.c, GCStackWalkPosix.c) unwinds without taking any lock a stopped thread may hold.
void gc_stack_walk(void* registers, uintptr_t top, gc_scan_range_fn now, gc_scan_range_fn later, void* ctx);
// The same for the calling thread from its caller's frame out: the registers it captures stay in its own frame, which is
// not read, so no copy of a dead value in a register is read from there. While an explicit collection runs, the frames
// below the one that called it (gc_stack_set_collection_caller) are stepped through unread.
void gc_stack_walk_own(uintptr_t top, gc_scan_range_fn scan, void* ctx);
// An explicit collection's own frames, below the program's call of it, are the collector's: they hold nothing of the
// program's, and what earlier calls left in their memory (an allocation's result in a prologue's alignment slot) or in a
// register they saved would keep objects alive. While it collects, the calling thread's walk steps through them without
// reading them, up to the frame waiting at `returnAddress` (the call's), read as any other; 0 when none runs. A walk
// that does not find that frame reads every frame.
void gc_stack_set_collection_caller(uintptr_t returnAddress);
uintptr_t gc_stack_collection_caller(void);
// Before the program's threads stop, with the collector's lock: what the walks of this collection need to know that only
// a lock a stopped thread may hold can tell (on Linux the loaded modules' unwind tables; nothing on Windows).
void gc_os_prepare_stack_walk(void);
