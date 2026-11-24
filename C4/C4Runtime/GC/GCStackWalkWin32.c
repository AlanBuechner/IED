#include "GC/GCStackMaps.h"
#include <windows.h>
#include <string.h>

// A stopped thread may hold the loader's lock or the dynamic function tables' (it was loading a library, or a script was
// registering its code), so the walk asks neither: VirtualQuery finds the image a pc is in, the image's own exception
// directory its function's unwind data, and RtlVirtualUnwind - which takes no lock - steps out of the frame. Code in no
// image (a script's) has no unwind data the walk can read: the stack from there on is read whole.

enum { kImagesCached = 16, kMostFrames = 8192, kLeafReturnBytes = 8, kRegisters = 16 };
// By CONTEXT's order (Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi, R8-R15): the callee-saved Rbx, Rbp, Rsi, Rdi, R12-R15 and
// Xmm6-15; the volatile Rax, Rcx, Rdx, R8-R11 and Xmm0-5.
static const uint32_t kKeptIntegers = (1u << 3) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 15);
static const uint32_t kKeptVectors = 0xFFC0u;
static const uint32_t kVolatileIntegers = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11);
static const uint32_t kVolatileVectors = 0x003Fu;

typedef struct Image
{
	uintptr_t base;
	uintptr_t end;
	const RUNTIME_FUNCTION* functions;
	DWORD count;
} Image;

typedef struct Images
{
	Image entry[kImagesCached];
	unsigned count;
	unsigned next; // the entry the next image replaces once all are used
} Images;

extern IMAGE_DOS_HEADER __ImageBase;

void gc_os_prepare_stack_walk(void) {}

void gc_os_stack_maps(const uint8_t** start, const uint8_t** end)
{
	// lld-link keeps the objects' `.c4smap` (Compiler/StackMapFormat.h) as one section of the image.
	*start = *end = NULL;
	const uint8_t* base = (const uint8_t*)&__ImageBase;
	const IMAGE_NT_HEADERS64* headers = (const IMAGE_NT_HEADERS64*)(base + __ImageBase.e_lfanew);
	const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(headers);
	for (WORD i = 0; i < headers->FileHeader.NumberOfSections; ++i, ++section)
		if (memcmp(section->Name, ".c4smap", IMAGE_SIZEOF_SHORT_NAME) == 0)
		{
			*start = base + section->VirtualAddress;
			*end = *start + section->Misc.VirtualSize;
			return;
		}
}

static int read_image(uintptr_t pc, Image* image)
{
	MEMORY_BASIC_INFORMATION info;
	if (!VirtualQuery((LPCVOID)pc, &info, sizeof(info)) || info.State != MEM_COMMIT || info.Type != MEM_IMAGE) return 0;
	const uint8_t* base = (const uint8_t*)info.AllocationBase;
	const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
	const IMAGE_NT_HEADERS64* headers = (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
	if (headers->Signature != IMAGE_NT_SIGNATURE || headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
	const IMAGE_DATA_DIRECTORY* exceptions = &headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
	image->base = (uintptr_t)base;
	image->end = (uintptr_t)base + headers->OptionalHeader.SizeOfImage;
	image->functions = (const RUNTIME_FUNCTION*)(base + exceptions->VirtualAddress);
	image->count = exceptions->VirtualAddress ? exceptions->Size / (DWORD)sizeof(RUNTIME_FUNCTION) : 0;
	return 1;
}

static const Image* image_of(Images* images, uintptr_t pc)
{
	for (unsigned i = 0; i < images->count; ++i)
		if (pc >= images->entry[i].base && pc < images->entry[i].end) return &images->entry[i];
	Image found;
	if (!read_image(pc, &found)) return NULL;
	unsigned at = images->count;
	if (at < kImagesCached) images->count++;
	else at = images->next++ % kImagesCached;
	images->entry[at] = found;
	return &images->entry[at];
}

// The function entry `pc` is in, by binary search of the image's sorted table; NULL in a leaf without one.
static PRUNTIME_FUNCTION function_of(const Image* image, uintptr_t pc)
{
	const DWORD rva = (DWORD)(pc - image->base);
	DWORD low = 0, high = image->count;
	while (low < high)
	{
		const DWORD middle = low + (high - low) / 2;
		const RUNTIME_FUNCTION* entry = &image->functions[middle];
		if (rva < entry->BeginAddress) high = middle;
		else if (rva >= entry->EndAddress) low = middle + 1;
		else
		{
			// An entry whose unwind data is marked with bit 0 is another entry's (RUNTIME_FUNCTION_INDIRECTION).
			if (entry->UnwindData & 1u) entry = (const RUNTIME_FUNCTION*)(image->base + (entry->UnwindData & ~3u));
			return (PRUNTIME_FUNCTION)entry;
		}
	}
	return NULL;
}

// `context` stepped out of its frame into its caller's, `saves` where the frame kept its callers' registers. A pc in an
// image with no function entry is a leaf, which only the innermost frame can be: its return address is on top of the
// stack.
static int unwind(Images* images, CONTEXT* context, int innermost, GCSaveSlots* saves)
{
	const Image* image = image_of(images, (uintptr_t)context->Rip);
	if (!image) return 0;
	PRUNTIME_FUNCTION function = function_of(image, (uintptr_t)context->Rip);
	if (function)
	{
		PVOID handlerData = NULL;
		DWORD64 establisher = 0;
		KNONVOLATILE_CONTEXT_POINTERS kept;
		memset(&kept, 0, sizeof(kept));
		RtlVirtualUnwind(UNW_FLAG_NHANDLER, (DWORD64)image->base, context->Rip, function, context, &handlerData, &establisher, &kept);
		for (unsigned i = 0; i < kRegisters; ++i)
		{
			if (kept.IntegerContext[i]) saves->saved |= 1u << i;
			if (kept.FloatingContext[i]) saves->savedVectors |= 1u << i;
			gc_save_slots_add(saves, (uintptr_t)kept.IntegerContext[i], sizeof(DWORD64));
			gc_save_slots_add(saves, (uintptr_t)kept.FloatingContext[i], sizeof(M128A));
		}
		return 1;
	}
	if (!innermost) return 0;
	context->Rip = *(const DWORD64*)context->Rsp;
	context->Rsp += kLeafReturnBytes;
	return 1;
}

static GCFrameState state_of(const CONTEXT* context)
{
	GCFrameState state;
	memset(&state, 0, sizeof(state));
	state.reg[3] = context->Rbx;
	state.reg[4] = context->Rsi;
	state.reg[5] = context->Rdi;
	state.reg[GC_REGISTER_FP] = context->Rbp;
	state.reg[GC_REGISTER_SP] = context->Rsp;
	state.reg[12] = context->R12;
	state.reg[13] = context->R13;
	state.reg[14] = context->R14;
	state.reg[15] = context->R15;
	state.reg[16] = context->Rip;
	// What an outer frame's registers are known to be: the callee-saved ones the unwinder restored.
	state.known = (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 15) | (1u << 16);
	return state;
}

// The registers `integers` and `vectors` name (a bit per register, in CONTEXT's order: Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi,
// Rdi, R8-R15; Xmm0-15), as `context` has them.
static void scan_registers(const CONTEXT* context, uint32_t integers, uint32_t vectors, gc_scan_range_fn now, void* ctx)
{
	const DWORD64* general = &context->Rax;
	const M128A* vector = &context->Xmm0;
	DWORD64 values[kRegisters];
	unsigned count = 0;
	for (unsigned i = 0; i < kRegisters; ++i)
	{
		if ((integers >> i) & 1u) values[count++] = general[i];
		if ((vectors >> i) & 1u) now(ctx, &vector[i], &vector[i] + 1);
	}
	now(ctx, values, values + count);
}

// The walk from `context`'s frame (number `first`: 0 is a stopped thread's innermost, the only one that may be a leaf).
static void walk(CONTEXT* context, Images* images, unsigned first, uintptr_t top, gc_scan_range_fn now, gc_scan_range_fn later, void* ctx)
{
	uintptr_t sp = (uintptr_t)context->Rsp;
	GCArgumentBytes owed = { 0, 0 };
	for (unsigned frame = first; sp < top; ++frame)
	{
		const int innermost = frame == 0;
		const GCStackRecord* record = innermost ? NULL : gc_stack_maps_find((uintptr_t)context->Rip);
		CONTEXT caller = *context;
		GCSaveSlots saves;
		saves.count = 0;
		saves.saved = saves.savedVectors = 0;
		if (frame >= kMostFrames || !unwind(images, &caller, innermost, &saves) || (uintptr_t)caller.Rsp <= sp || (uintptr_t)caller.Rsp > top)
			break; // the rest of the stack whole, from this frame on
		const GCFrameState state = state_of(context);
		if (record && gc_stack_record_scan(record, &state, now, later, ctx)) gc_arguments_in_mapped(&owed, sp, (uintptr_t)caller.Rsp, &saves, later, ctx);
		else
		{
			gc_scan_frame_whole(sp, (uintptr_t)caller.Rsp, &saves, later, ctx);
			scan_registers(context, saves.saved & kKeptIntegers, saves.savedVectors & kKeptVectors, now, ctx);
			gc_arguments_after_whole(&owed, (uintptr_t)caller.Rsp, top);
		}
		*context = caller;
		sp = (uintptr_t)caller.Rsp;
		if (!caller.Rip) break; // past the thread's first frame: what is above it, whole
	}
	// Past the walk the registers no walked frame saved are the outer frames' own.
	if (sp < top) later(ctx, (const void*)sp, (const void*)top);
	scan_registers(context, kKeptIntegers, kKeptVectors, now, ctx);
}

void gc_stack_walk(void* registers, uintptr_t top, gc_scan_range_fn now, gc_scan_range_fn later, void* ctx)
{
	CONTEXT* context = (CONTEXT*)registers;
	// The innermost frame was interrupted anywhere: what its volatile registers hold is its own.
	scan_registers(context, kVolatileIntegers, kVolatileVectors, now, ctx);
	Images images;
	images.count = images.next = 0;
	walk(context, &images, 0, top, now, later, ctx);
}

// `context` stepped out to the frame waiting at `pc`, through the frames below it, unread; false, unchanged, when no frame
// of the stack is waiting there.
static int step_to(CONTEXT* context, Images* images, uintptr_t pc, uintptr_t top)
{
	CONTEXT at = *context;
	for (unsigned frame = 0; frame < kMostFrames; ++frame)
	{
		if ((uintptr_t)at.Rip == pc)
		{
			*context = at;
			return 1;
		}
		CONTEXT caller = at;
		GCSaveSlots saves;
		saves.count = 0;
		saves.saved = saves.savedVectors = 0;
		if (!unwind(images, &caller, 0, &saves) || (uintptr_t)caller.Rsp <= (uintptr_t)at.Rsp || (uintptr_t)caller.Rsp > top || !caller.Rip) return 0;
		at = caller;
	}
	return 0;
}

__declspec(noinline) void gc_stack_walk_own(uintptr_t top, gc_scan_range_fn scan, void* ctx)
{
	CONTEXT context;
	memset(&context, 0, sizeof(context));
	RtlCaptureContext(&context);
	Images images;
	images.count = images.next = 0;
	GCSaveSlots saves;
	saves.count = 0;
	saves.saved = saves.savedVectors = 0;
	CONTEXT caller = context;
	// Without unwind data for this frame (never, built as the runtime is) the walk reads it too, its copies included.
	if (unwind(&images, &caller, 0, &saves) && (uintptr_t)caller.Rsp > (uintptr_t)context.Rsp && (uintptr_t)caller.Rsp < top) context = caller;
	if (gc_stack_collection_caller()) step_to(&context, &images, gc_stack_collection_caller(), top);
	walk(&context, &images, 1, top, scan, scan, ctx);
}
