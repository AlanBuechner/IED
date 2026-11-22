// The collector's stack walk on Linux (GCStackMaps.h). libgcc's unwinder finds a function's unwind data through
// dl_iterate_phdr, whose lock a stopped thread may hold, so the walk reads the data itself: before the threads stop the
// collector notes each loaded module's code and its .eh_frame_hdr (gc_os_prepare_stack_walk); a frame's FDE is found in
// that table and its call frame instructions are run up to the frame's pc. Code in no noted module (a script's, a
// library loaded since) and unwind data the walk does not read (a DWARF expression, a signal frame) end the walk: the
// stack from there on is read whole.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "GC/GCStackMaps.h"
#include <link.h>
#include <string.h>
#include <ucontext.h>

enum { kMostFrames = 8192, kRedZoneBytes = 128, kStates = 8, kReturnAddress = 16 };
enum { kSame = 0, kUndefined, kOffset, kValueOffset, kRegisterRule };
// The registers a System V function keeps for its callers, and the stack pointer.
#define GC_KEPT_REGISTERS ((1u << 3) | (1u << 6) | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 15))

extern const uint8_t __start_llvm_stackmaps[] __attribute__((weak, visibility("hidden")));
extern const uint8_t __stop_llvm_stackmaps[] __attribute__((weak, visibility("hidden")));

void gc_os_stack_maps(const uint8_t** start, const uint8_t** end)
{
	// Compiler/StackMaps.cpp names each object's stack map `llvm_stackmaps`, which the linker brackets.
	*start = __start_llvm_stackmaps;
	*end = __stop_llvm_stackmaps;
}

// --- the modules, noted before the threads stop --------------------------------------------------------------------

typedef struct Module
{
	uintptr_t low;
	uintptr_t high;
	const uint8_t* table; // its .eh_frame_hdr
} Module;

static Module* g_modules;
static size_t g_moduleCount;
static size_t g_moduleCapacity;

static int note_module(struct dl_phdr_info* info, size_t size, void* data)
{
	(void)size; (void)data;
	const ElfW(Phdr)* frames = NULL;
	uintptr_t low = UINTPTR_MAX, high = 0;
	for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i)
	{
		const ElfW(Phdr)* header = &info->dlpi_phdr[i];
		if (header->p_type == PT_GNU_EH_FRAME) frames = header;
		else if (header->p_type == PT_LOAD && (header->p_flags & PF_X))
		{
			const uintptr_t start = (uintptr_t)info->dlpi_addr + header->p_vaddr;
			if (start < low) low = start;
			if (start + header->p_memsz > high) high = start + header->p_memsz;
		}
	}
	if (!frames || high <= low) return 0;
	if (g_moduleCount == g_moduleCapacity)
	{
		const size_t capacity = g_moduleCapacity ? g_moduleCapacity * 2 : 32;
		Module* grown = (Module*)gc_mem_realloc(g_modules, capacity * sizeof(Module));
		if (!grown) return 1;
		g_modules = grown;
		g_moduleCapacity = capacity;
	}
	g_modules[g_moduleCount].low = low;
	g_modules[g_moduleCount].high = high;
	g_modules[g_moduleCount].table = (const uint8_t*)(info->dlpi_addr + frames->p_vaddr);
	g_moduleCount++;
	return 0;
}

void gc_os_prepare_stack_walk(void)
{
	g_moduleCount = 0;
	dl_iterate_phdr(note_module, NULL);
}

static const Module* module_of(uintptr_t pc)
{
	for (size_t i = 0; i < g_moduleCount; ++i)
		if (pc >= g_modules[i].low && pc < g_modules[i].high) return &g_modules[i];
	return NULL;
}

// --- reading DWARF ---------------------------------------------------------------------------------------------------

typedef struct Reader
{
	const uint8_t* at;
	const uint8_t* end;
	int bad;
} Reader;

static uint8_t byte(Reader* r)
{
	if (r->at >= r->end) { r->bad = 1; return 0; }
	return *r->at++;
}

static uint64_t fixed(Reader* r, unsigned bytes)
{
	if ((size_t)(r->end - r->at) < bytes) { r->bad = 1; return 0; }
	uint64_t value = 0;
	memcpy(&value, r->at, bytes);
	r->at += bytes;
	return value;
}

static uint64_t uleb(Reader* r)
{
	uint64_t value = 0;
	for (unsigned shift = 0; ; shift += 7)
	{
		const uint8_t b = byte(r);
		if (shift < 64) value |= (uint64_t)(b & 0x7fu) << shift;
		if (!(b & 0x80u) || r->bad) return value;
	}
}

static int64_t sleb(Reader* r)
{
	int64_t value = 0;
	unsigned shift = 0;
	uint8_t b;
	do
	{
		b = byte(r);
		if (shift < 64) value |= (int64_t)(b & 0x7fu) << shift;
		shift += 7;
	} while ((b & 0x80u) && !r->bad);
	if (shift < 64 && (b & 0x40u)) value |= -((int64_t)1 << shift);
	return value;
}

// A pointer encoded as DW_EH_PE `encoding` says (absolute, or relative to where it is or to `dataBase`); 0 for one the
// walk does not read (bad).
static uintptr_t encoded(Reader* r, uint8_t encoding, uintptr_t dataBase)
{
	if (encoding == 0xff) return 0; // omitted
	const uintptr_t at = (uintptr_t)r->at;
	uint64_t value = 0;
	switch (encoding & 0x0f)
	{
	case 0x00: value = fixed(r, 8); break;
	case 0x01: value = uleb(r); break;
	case 0x02: value = fixed(r, 2); break;
	case 0x03: value = fixed(r, 4); break;
	case 0x04: value = fixed(r, 8); break;
	case 0x09: value = (uint64_t)sleb(r); break;
	case 0x0a: value = (uint64_t)(int64_t)(int16_t)fixed(r, 2); break;
	case 0x0b: value = (uint64_t)(int64_t)(int32_t)fixed(r, 4); break;
	case 0x0c: value = fixed(r, 8); break;
	default: r->bad = 1; return 0;
	}
	switch (encoding & 0x70)
	{
	case 0x00: break;
	case 0x10: value += at; break;
	case 0x30: value += dataBase; break;
	default: r->bad = 1; return 0;
	}
	if (encoding & 0x80) value = *(const uintptr_t*)(uintptr_t)value;
	return (uintptr_t)value;
}

typedef struct Cie
{
	uint64_t codeAlign;
	int64_t dataAlign;
	uint64_t returnRegister;
	uint8_t fdeEncoding;
	int augmented;
	const uint8_t* instructions;
	const uint8_t* end;
} Cie;

// An entry's bounds: its length, then what it holds up to `*end`.
static const uint8_t* entry_body(const uint8_t* at, const uint8_t** end)
{
	uint64_t length = 0;
	memcpy(&length, at, 4);
	at += 4;
	if (length == 0xffffffffu)
	{
		memcpy(&length, at, 8);
		at += 8;
	}
	*end = at + length;
	return at;
}

static int read_cie(const uint8_t* at, Cie* cie)
{
	const uint8_t* end = NULL;
	at = entry_body(at, &end);
	Reader r = { at, end, 0 };
	if (fixed(&r, 4) != 0) return 0; // .eh_frame's CIE id
	const uint8_t version = byte(&r);
	const char* augmentation = (const char*)r.at;
	const size_t length = strnlen(augmentation, (size_t)(r.end - r.at));
	r.at += length + 1;
	cie->codeAlign = uleb(&r);
	cie->dataAlign = sleb(&r);
	cie->returnRegister = version == 1 ? byte(&r) : uleb(&r);
	cie->fdeEncoding = 0;
	cie->augmented = augmentation[0] == 'z';
	if (cie->augmented)
	{
		const uint64_t bytes = uleb(&r);
		const uint8_t* data = r.at;
		for (const char* a = augmentation + 1; *a && !r.bad; ++a)
		{
			if (*a == 'R') cie->fdeEncoding = byte(&r);
			else if (*a == 'L') (void)byte(&r);
			else if (*a == 'P')
			{
				const uint8_t encoding = byte(&r);
				(void)encoded(&r, (uint8_t)(encoding & 0x7f), 0); // stepped over, not followed
			}
			else if (*a == 'S') return 0; // a signal frame: the walk does not read one
			else if (*a != 'B') return 0;
		}
		r.at = data + bytes;
	}
	else if (augmentation[0]) return 0;
	cie->instructions = r.at;
	cie->end = end;
	return !r.bad && r.at <= end;
}

// The FDE whose code holds `pc`, by binary search of the module's .eh_frame_hdr table (datarel sdata4, as linkers write
// it); NULL when there is none.
static const uint8_t* fde_of(const Module* module, uintptr_t pc)
{
	const uint8_t* table = module->table;
	if (table[0] != 1 || table[3] != 0x3b) return NULL;
	Reader r = { table + 4, table + 64, 0 };
	(void)encoded(&r, table[1], (uintptr_t)table);
	const uintptr_t count = encoded(&r, table[2], (uintptr_t)table);
	if (r.bad || !count) return NULL;
	const int32_t* entries = (const int32_t*)r.at;
	size_t low = 0, high = count;
	while (high - low > 1)
	{
		const size_t middle = low + (high - low) / 2;
		if ((uintptr_t)(table + entries[2 * middle]) <= pc) low = middle;
		else high = middle;
	}
	if ((uintptr_t)(table + entries[2 * low]) > pc) return NULL;
	return table + entries[2 * low + 1];
}

// --- running the call frame instructions ----------------------------------------------------------------------------

typedef struct Rule
{
	uint8_t kind;
	int64_t value;
} Rule;

typedef struct Rules
{
	Rule reg[GC_FRAME_REGISTERS];
	uint64_t cfaRegister;
	int64_t cfaOffset;
} Rules;

static void set_rule(Rules* rules, uint64_t reg, uint8_t kind, int64_t value)
{
	if (reg >= GC_FRAME_REGISTERS) return; // a vector register: no pointer the walk follows
	rules->reg[reg].kind = kind;
	rules->reg[reg].value = value;
}

// Runs [at, end) from `location` while the location is at most `target`; `initial` is what DW_CFA_restore returns to. 0
// when an instruction is one the walk does not read.
static int run(const uint8_t* at, const uint8_t* end, const Cie* cie, uintptr_t location, uintptr_t target, Rules* rules, const Rules* initial)
{
	Rules saved[kStates];
	unsigned depth = 0;
	Reader r = { at, end, 0 };
	while (r.at < r.end && !r.bad)
	{
		const uint8_t op = byte(&r);
		const uint8_t low = op & 0x3fu;
		switch (op >> 6)
		{
		case 1:
			location += low * cie->codeAlign;
			if (location > target) return 1;
			continue;
		case 2: set_rule(rules, low, kOffset, (int64_t)uleb(&r) * cie->dataAlign); continue;
		case 3: if (low < GC_FRAME_REGISTERS) rules->reg[low] = initial->reg[low]; continue;
		default: break;
		}
		uint64_t reg = 0;
		switch (op)
		{
		case 0x00: break;
		case 0x01: location = encoded(&r, cie->fdeEncoding, 0); if (location > target) return 1; break;
		case 0x02: location += fixed(&r, 1) * cie->codeAlign; if (location > target) return 1; break;
		case 0x03: location += fixed(&r, 2) * cie->codeAlign; if (location > target) return 1; break;
		case 0x04: location += fixed(&r, 4) * cie->codeAlign; if (location > target) return 1; break;
		case 0x05: reg = uleb(&r); set_rule(rules, reg, kOffset, (int64_t)uleb(&r) * cie->dataAlign); break;
		case 0x06: reg = uleb(&r); if (reg < GC_FRAME_REGISTERS) rules->reg[reg] = initial->reg[reg]; break;
		case 0x07: set_rule(rules, uleb(&r), kUndefined, 0); break;
		case 0x08: set_rule(rules, uleb(&r), kSame, 0); break;
		case 0x09: reg = uleb(&r); set_rule(rules, reg, kRegisterRule, (int64_t)uleb(&r)); break;
		case 0x0a: if (depth == kStates) return 0; saved[depth++] = *rules; break;
		case 0x0b: if (!depth) return 0; *rules = saved[--depth]; break;
		case 0x0c: rules->cfaRegister = uleb(&r); rules->cfaOffset = (int64_t)uleb(&r); break;
		case 0x0d: rules->cfaRegister = uleb(&r); break;
		case 0x0e: rules->cfaOffset = (int64_t)uleb(&r); break;
		case 0x11: reg = uleb(&r); set_rule(rules, reg, kOffset, sleb(&r) * cie->dataAlign); break;
		case 0x12: rules->cfaRegister = uleb(&r); rules->cfaOffset = sleb(&r) * cie->dataAlign; break;
		case 0x13: rules->cfaOffset = sleb(&r) * cie->dataAlign; break;
		case 0x14: reg = uleb(&r); set_rule(rules, reg, kValueOffset, (int64_t)uleb(&r) * cie->dataAlign); break;
		case 0x15: reg = uleb(&r); set_rule(rules, reg, kValueOffset, sleb(&r) * cie->dataAlign); break;
		case 0x2e: (void)uleb(&r); break; // DW_CFA_GNU_args_size
		case 0x2f: reg = uleb(&r); set_rule(rules, reg, kOffset, -(int64_t)uleb(&r) * cie->dataAlign); break;
		default: return 0; // a DWARF expression (0x0f, 0x10, 0x16) or an unknown instruction
		}
	}
	return !r.bad;
}

// `state` stepped out of its frame into its caller's, `saves` where the frame kept its callers' registers; 0 when no
// unwind data the walk reads says how, the stack ends, or a register would be read from outside the stack below `top`.
static int step(GCFrameState* state, int innermost, uintptr_t top, GCSaveSlots* saves)
{
	// A return address may be one past its function's end (a call that does not return): the call is what is looked up.
	const uintptr_t pc = (uintptr_t)state->reg[kReturnAddress] - (innermost ? 0 : 1);
	const Module* module = module_of(pc);
	const uint8_t* fde = module ? fde_of(module, pc) : NULL;
	if (!fde) return 0;
	const uint8_t* end = NULL;
	const uint8_t* body = entry_body(fde, &end);
	uint32_t backward = 0;
	memcpy(&backward, body, 4);
	Cie cie;
	if (!backward || !read_cie(body - backward, &cie)) return 0;
	Reader r = { body + 4, end, 0 };
	const uintptr_t begin = encoded(&r, cie.fdeEncoding, 0);
	const uintptr_t range = encoded(&r, (uint8_t)(cie.fdeEncoding & 0x0f), 0);
	if (r.bad || pc < begin || pc >= begin + range) return 0;
	if (cie.augmented)
	{
		const uint64_t skipped = uleb(&r);
		r.at += skipped;
	}
	Rules initial;
	memset(&initial, 0, sizeof(initial));
	if (!run(cie.instructions, cie.end, &cie, begin, UINTPTR_MAX, &initial, &initial)) return 0;
	Rules rules = initial;
	if (!run(r.at, end, &cie, begin, pc, &rules, &initial)) return 0;
	if (rules.cfaRegister >= GC_FRAME_REGISTERS || !(state->known & (1u << rules.cfaRegister))) return 0;
	const uintptr_t cfa = (uintptr_t)((int64_t)state->reg[rules.cfaRegister] + rules.cfaOffset);
	const uintptr_t low = (uintptr_t)state->reg[GC_REGISTER_SP] - kRedZoneBytes;
	if (cfa <= (uintptr_t)state->reg[GC_REGISTER_SP] || cfa > top) return 0;
	GCFrameState caller = *state;
	caller.known &= GC_KEPT_REGISTERS | (1u << GC_REGISTER_SP);
	for (unsigned reg = 0; reg < GC_FRAME_REGISTERS; ++reg)
	{
		const Rule rule = rules.reg[reg];
		const uintptr_t at = (uintptr_t)((int64_t)cfa + rule.value);
		switch (rule.kind)
		{
		case kOffset:
			if (at < low || at + sizeof(uint64_t) > top) return 0;
			caller.reg[reg] = *(const uint64_t*)at;
			caller.known |= 1u << reg;
			if (GC_KEPT_REGISTERS & (1u << reg))
			{
				saves->saved |= 1u << reg;
				gc_save_slots_add(saves, at, sizeof(uint64_t));
			}
			break;
		case kValueOffset: caller.reg[reg] = at; caller.known |= 1u << reg; break;
		case kRegisterRule:
			if (rule.value < GC_FRAME_REGISTERS && (state->known & (1u << rule.value))) { caller.reg[reg] = state->reg[rule.value]; caller.known |= 1u << reg; }
			else caller.known &= ~(1u << reg);
			break;
		case kUndefined: caller.known &= ~(1u << reg); break;
		default: break; // the same value
		}
	}
	if (cie.returnRegister >= GC_FRAME_REGISTERS || rules.reg[cie.returnRegister].kind == kUndefined) return 0;
	caller.reg[kReturnAddress] = caller.reg[cie.returnRegister];
	caller.known |= 1u << kReturnAddress;
	caller.reg[GC_REGISTER_SP] = cfa;
	caller.known |= 1u << GC_REGISTER_SP;
	*state = caller;
	return 1;
}

// --- the walk --------------------------------------------------------------------------------------------------------

static GCFrameState state_of(const mcontext_t* context)
{
	static const int places[GC_FRAME_REGISTERS] = { REG_RAX, REG_RDX, REG_RCX, REG_RBX, REG_RSI, REG_RDI, REG_RBP, REG_RSP,
		REG_R8, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15, REG_RIP };
	GCFrameState state;
	for (unsigned i = 0; i < GC_FRAME_REGISTERS; ++i) state.reg[i] = (uint64_t)context->gregs[places[i]];
	state.known = (1u << GC_FRAME_REGISTERS) - 1u;
	return state;
}

// The registers `which` names (a bit per DWARF number) that `state` knows.
static void scan_registers(const GCFrameState* state, uint32_t which, gc_scan_range_fn now, void* ctx)
{
	uint64_t values[GC_FRAME_REGISTERS];
	unsigned count = 0;
	for (unsigned reg = 0; reg < GC_FRAME_REGISTERS; ++reg)
		if ((which & state->known & (1u << reg))) values[count++] = state->reg[reg];
	now(ctx, values, values + count);
}

// The walk from `state`'s frame (number `first`: 0 is a stopped thread's innermost, the only one that may keep data in
// the red zone), whose stack is read from `from`.
static void walk(GCFrameState state, unsigned first, uintptr_t from, uintptr_t top, gc_scan_range_fn now, gc_scan_range_fn later, void* ctx)
{
	uintptr_t sp = (uintptr_t)state.reg[GC_REGISTER_SP];
	for (unsigned frame = first; sp < top; ++frame)
	{
		const int innermost = frame == 0;
		const GCStackRecord* record = innermost ? NULL : gc_stack_maps_find((uintptr_t)state.reg[kReturnAddress]);
		GCFrameState caller = state;
		GCSaveSlots saves;
		saves.count = 0;
		saves.saved = saves.savedVectors = 0;
		if (frame >= kMostFrames || !step(&caller, innermost, top, &saves) || caller.reg[GC_REGISTER_SP] <= sp || caller.reg[GC_REGISTER_SP] > top)
			break; // the rest of the stack whole, from this frame on
		const uintptr_t callerSp = (uintptr_t)caller.reg[GC_REGISTER_SP];
		if (!record || !gc_stack_record_scan(record, &state, now, later, ctx))
		{
			const uintptr_t end = callerSp + GC_FRAME_ARGUMENT_BYTES < top ? callerSp + GC_FRAME_ARGUMENT_BYTES : top;
			gc_scan_frame_whole(from, end, &saves, later, ctx);
			scan_registers(&state, saves.saved, now, ctx);
		}
		state = caller;
		sp = from = callerSp;
		if (!state.reg[kReturnAddress]) break; // past the thread's first frame: what is above it, whole
	}
	// Past the walk the registers no walked frame saved are the outer frames' own.
	if (from < top) later(ctx, (const void*)from, (const void*)top);
	scan_registers(&state, GC_KEPT_REGISTERS, now, ctx);
}

void gc_stack_walk(void* registers, uintptr_t top, gc_scan_range_fn now, gc_scan_range_fn later, void* ctx)
{
	const GCFrameState state = state_of((const mcontext_t*)registers);
	// The innermost frame was interrupted anywhere: what its volatile registers hold is its own.
	scan_registers(&state, ~(GC_KEPT_REGISTERS | (1u << GC_REGISTER_SP) | (1u << kReturnAddress)) & ((1u << GC_FRAME_REGISTERS) - 1u), now, ctx);
	walk(state, 0, (uintptr_t)state.reg[GC_REGISTER_SP] - kRedZoneBytes, top, now, later, ctx);
}

__attribute__((noinline)) void gc_stack_walk_own(uintptr_t top, gc_scan_range_fn scan, void* ctx)
{
	ucontext_t registers;
	getcontext(&registers);
	GCFrameState state = state_of(&registers.uc_mcontext);
	GCFrameState caller = state;
	GCSaveSlots saves;
	saves.count = 0;
	saves.saved = saves.savedVectors = 0;
	// Without unwind data for this frame (never, built as the runtime is) the walk reads it too, its copies included.
	if (step(&caller, 0, top, &saves) && caller.reg[GC_REGISTER_SP] > state.reg[GC_REGISTER_SP] && caller.reg[GC_REGISTER_SP] < top) state = caller;
	walk(state, 1, (uintptr_t)state.reg[GC_REGISTER_SP], top, scan, scan, ctx);
}
