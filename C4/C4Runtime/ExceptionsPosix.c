// Exceptions on Linux (D-86). C4 code unwinds by landing pads on libgcc's unwinder (Backend/CodeGen/RegionLandingPads.cpp);
// this is the rest: the exception a throw makes, its raise, the personality routine that finds a function's landing
// pad and the clause that takes the exception (from the function's LSDA), and what a handler does with it (begin and end
// its catch, rethrow it). Which types a throw can be caught as, and how a handler gets its copy, are the ThrowInfo's,
// the same tables MSVC's runtime reads on Windows (RttiTablesC.h).
//
// The stack unwinds before a handler runs, so the thrown object cannot stay in the thrower's frame: it lives behind a
// header in memory of its own, which the collector scans as a root until the last catch of it ends.

#include "GCRuntimeC.h"
#include "PlatformRuntimeC.h"
#include "RttiTablesC.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unwind.h>

#define C4_EXCEPTION_CLASS 0x4334455843000000ull /* "C4EXC\0\0\0": a C4 exception, not C++'s or another's */

enum { kThrowFrames = 62 };

typedef struct C4Exception
{
	const ThrowInfo* throwInfo;
	void* frames[kThrowFrames];        // the stack it was thrown from, for a report made in its handler (c4_eh_handling)
	int32_t frameCount;
	size_t size;                       // the object's bytes
	int32_t handlers;                  // catches holding it
	int32_t rethrown;                  // `throw;` sent it on: the catch that ends now does not destroy it
	struct C4Exception* nextCaught;    // the thread's caught exceptions, innermost first
	struct _Unwind_Exception unwind;   // what the unwinder carries; the object follows the header
} C4Exception;

// A foreign exception (C++'s) caught by `catch (...)`: only its end is the runtime's.
typedef struct Caught
{
	C4Exception* exception;            // null for a foreign one
	struct _Unwind_Exception* foreign;
	int32_t rethrown;                  // a foreign one sent on by `throw;`: its end is not this catch's
	struct Caught* next;
} Caught;

static _Thread_local Caught* t_caught;

void c4_report_uncaught_exception(const char* what); // EHRuntime.cpp

static C4Exception* header_of_object(void* object) { return (C4Exception*)((uint8_t*)object - sizeof(C4Exception)); }
static void* object_of(C4Exception* exception) { return (uint8_t*)exception + sizeof(C4Exception); }

static C4Exception* c4_exception_of(struct _Unwind_Exception* unwind)
{
	if (!unwind || unwind->exception_class != C4_EXCEPTION_CLASS) return NULL;
	return (C4Exception*)((uint8_t*)unwind - offsetof(C4Exception, unwind));
}

// --- the exception object ---------------------------------------------------------------------------------------

void* c4_eh_allocate(uint64_t size)
{
	C4Exception* exception = (C4Exception*)calloc(1, sizeof(C4Exception) + (size_t)size);
	if (!exception) c4_fatal("out of memory throwing an exception");
	exception->size = (size_t)size;
	void* object = object_of(exception);
	c4_gc_register_region(object, (size_t)size);
	return object;
}

typedef void (*Destructor)(void* self);
typedef void (*CopyConstructor)(void* self, const void* other);

static void destroy(C4Exception* exception)
{
	void* object = object_of(exception);
	if (exception->throwInfo)
	{
		const Destructor run = (const Destructor)c4_table_ref(NULL, &exception->throwInfo->destroy);
		if (run) run(object);
	}
	c4_gc_unregister_region(object);
	free(exception);
}

// _Unwind_DeleteException's: a foreign runtime ended a catch of it.
static void cleanup_unwind(_Unwind_Reason_Code reason, struct _Unwind_Exception* unwind)
{
	(void)reason;
	C4Exception* exception = c4_exception_of(unwind);
	if (exception) destroy(exception);
}

static void uncaught(void)
{
	// Nothing unwound yet: the report holds the stack of the throw.
	c4_report_uncaught_exception("an exception was thrown and not caught");
	exit(1);
}

typedef struct FrameWalk
{
	C4Exception* exception;
	int32_t skip;
} FrameWalk;

static _Unwind_Reason_Code record_frame(struct _Unwind_Context* context, void* data)
{
	FrameWalk* walk = (FrameWalk*)data;
	const uintptr_t ip = _Unwind_GetIP(context);
	if (!ip || walk->exception->frameCount >= kThrowFrames) return _URC_END_OF_STACK;
	if (walk->skip > 0)
	{
		walk->skip -= 1;
		return _URC_NO_REASON;
	}
	walk->exception->frames[walk->exception->frameCount++] = (void*)ip;
	return _URC_NO_REASON;
}

__attribute__((noinline)) void c4_eh_throw(void* object, const void* throwInfo)
{
	C4Exception* exception = header_of_object(object);
	exception->throwInfo = (const ThrowInfo*)throwInfo;
	FrameWalk walk = { exception, 1 }; // from the throw, this function left out
	_Unwind_Backtrace(record_frame, &walk);
	exception->unwind.exception_class = C4_EXCEPTION_CLASS;
	exception->unwind.exception_cleanup = cleanup_unwind;
	_Unwind_RaiseException(&exception->unwind);
	uncaught();
}

void c4_eh_rethrow(void)
{
	Caught* top = t_caught;
	if (!top) c4_fatal("`throw;` outside a catch handler");
	if (top->exception) top->exception->rethrown = 1;
	else top->rethrown = 1;
	_Unwind_RaiseException(top->exception ? &top->exception->unwind : top->foreign);
	uncaught();
}

// --- catching ---------------------------------------------------------------------------------------------------

static int same_type(const TypeDescriptor* a, const TypeDescriptor* b)
{
	return a == b || strcmp(a->name, b->name) == 0;
}

// The type `exception` may be caught as that `handler` names, or null when it names none of them.
static const CatchableType* catchable_as(const C4Exception* exception, const TypeDescriptor* handler)
{
	if (!exception->throwInfo) return NULL;
	const CatchableTypeArray* types = (const CatchableTypeArray*)c4_table_ref(NULL, &exception->throwInfo->catchableTypes);
	for (int32_t i = 0; types && i < types->count; ++i)
	{
		const CatchableType* type = (const CatchableType*)c4_table_ref(NULL, &types->types[i]);
		const TypeDescriptor* name = (const TypeDescriptor*)c4_table_ref(NULL, &type->typeDescriptor);
		if (name && same_type(name, handler)) return type;
	}
	return NULL;
}

// The clause's copy into its slot, as MSVC's frame handler makes it: by reference, the address of the sub-object;
// a simple type copied as bits, a pointer adjusted to its base; an object by its copy constructor, or as bits.
static void copy_into_slot(C4Exception* exception, const CatchableType* type, int32_t adjectives, void* slot)
{
	uint8_t* object = (uint8_t*)object_of(exception);
	if (type->properties & kCatchableSimpleType)
	{
		if (adjectives & kHandlerReference) *(void**)slot = object;
		else
		{
			memcpy(slot, object, (size_t)type->size);
			if (type->size == (int32_t)sizeof(void*) && *(uint8_t**)slot) *(uint8_t**)slot += type->mdisp;
		}
		return;
	}
	uint8_t* subObject = object + type->mdisp;
	if (adjectives & kHandlerReference)
	{
		*(void**)slot = subObject;
		return;
	}
	const CopyConstructor copy = (const CopyConstructor)c4_table_ref(NULL, &type->copy);
	if (copy) copy(slot, subObject);
	else memcpy(slot, subObject, (size_t)type->size);
}

int32_t c4_eh_handling(const char** typeName, void* const** frames)
{
	const Caught* caught = t_caught;
	if (!caught || !caught->exception) return 0;
	const C4Exception* exception = caught->exception;
	*typeName = NULL;
	if (exception->throwInfo)
	{
		const CatchableTypeArray* types = (const CatchableTypeArray*)c4_table_ref(NULL, &exception->throwInfo->catchableTypes);
		const CatchableType* first = types && types->count > 0 ? (const CatchableType*)c4_table_ref(NULL, &types->types[0]) : NULL;
		const TypeDescriptor* name = first ? (const TypeDescriptor*)c4_table_ref(NULL, &first->typeDescriptor) : NULL;
		if (name) *typeName = name->name;
	}
	*frames = exception->frames;
	return exception->frameCount;
}

void c4_eh_begin_catch(void* unwind, const void* typeDescriptor, int32_t adjectives, void* slot)
{
	Caught* caught = (Caught*)malloc(sizeof(Caught));
	if (!caught) c4_fatal("out of memory catching an exception");
	caught->exception = c4_exception_of((struct _Unwind_Exception*)unwind);
	caught->foreign = caught->exception ? NULL : (struct _Unwind_Exception*)unwind;
	caught->rethrown = 0;
	caught->next = t_caught;
	t_caught = caught;
	C4Exception* exception = caught->exception;
	if (!exception) return; // only `catch (...)` takes a foreign exception
	exception->handlers += 1;
	exception->rethrown = 0;
	if (!typeDescriptor || !slot) return;
	const CatchableType* type = catchable_as(exception, (const TypeDescriptor*)typeDescriptor);
	if (type) copy_into_slot(exception, type, adjectives, slot);
}

void c4_eh_end_catch(void)
{
	Caught* caught = t_caught;
	if (!caught) return;
	t_caught = caught->next;
	C4Exception* exception = caught->exception;
	struct _Unwind_Exception* foreign = caught->foreign;
	const int32_t sentOn = caught->rethrown;
	free(caught);
	if (foreign)
	{
		if (!sentOn) _Unwind_DeleteException(foreign);
		return;
	}
	if (--exception->handlers > 0) return;
	if (exception->rethrown)
	{
		exception->rethrown = 0; // in flight again: the catch that takes it next owns it
		return;
	}
	destroy(exception);
}

// --- the personality routine ------------------------------------------------------------------------------------

enum
{
	kOmit = 0xff,
	kFormat = 0x0f,
	kApplication = 0x70,
	kIndirect = 0x80,
	kAbsolute = 0x00, kUleb128 = 0x01, kUdata2 = 0x02, kUdata4 = 0x03, kUdata8 = 0x04,
	kSleb128 = 0x09, kSdata2 = 0x0a, kSdata4 = 0x0b, kSdata8 = 0x0c,
	kPcRelative = 0x10,
};

static uintptr_t read_uleb128(const uint8_t** at)
{
	uintptr_t value = 0;
	unsigned shift = 0;
	uint8_t byte;
	do
	{
		byte = *(*at)++;
		value |= (uintptr_t)(byte & 0x7f) << shift;
		shift += 7;
	} while (byte & 0x80);
	return value;
}

static intptr_t read_sleb128(const uint8_t** at)
{
	uintptr_t value = 0;
	unsigned shift = 0;
	uint8_t byte;
	do
	{
		byte = *(*at)++;
		value |= (uintptr_t)(byte & 0x7f) << shift;
		shift += 7;
	} while (byte & 0x80);
	if ((byte & 0x40) && shift < sizeof(value) * 8) value |= ~(uintptr_t)0 << shift;
	return (intptr_t)value;
}

static size_t encoded_size(uint8_t encoding)
{
	switch (encoding & kFormat)
	{
	case kUdata2: case kSdata2: return 2;
	case kUdata4: case kSdata4: return 4;
	default: return 8;
	}
}

// A pointer in the DWARF EH encoding `encoding` (absolute or PC-relative, possibly through a pointer).
static uintptr_t read_encoded(const uint8_t** at, uint8_t encoding)
{
	const uint8_t* start = *at;
	uintptr_t value = 0;
	switch (encoding & kFormat)
	{
	case kAbsolute: case kUdata8: case kSdata8: memcpy(&value, *at, 8); *at += 8; break;
	case kUleb128: value = read_uleb128(at); break;
	case kSleb128: value = (uintptr_t)read_sleb128(at); break;
	case kUdata2: { uint16_t v; memcpy(&v, *at, 2); *at += 2; value = v; break; }
	case kSdata2: { int16_t v; memcpy(&v, *at, 2); *at += 2; value = (uintptr_t)(intptr_t)v; break; }
	case kUdata4: { uint32_t v; memcpy(&v, *at, 4); *at += 4; value = v; break; }
	case kSdata4: { int32_t v; memcpy(&v, *at, 4); *at += 4; value = (uintptr_t)(intptr_t)v; break; }
	default: c4_fatal("an exception table uses an encoding the C4 runtime does not read");
	}
	if (value && (encoding & kApplication) == kPcRelative) value += (uintptr_t)start;
	if (value && (encoding & kIndirect)) value = *(const uintptr_t*)value;
	return value;
}

typedef struct Lsda
{
	uintptr_t landingPadBase;
	uint8_t typeEncoding;
	const uint8_t* typeTable;     // its end: entry i (from 1) is i entries before it
	uint8_t callSiteEncoding;
	const uint8_t* callSites;
	const uint8_t* actions;       // right after the call sites
} Lsda;

static Lsda read_header(const uint8_t* at, uintptr_t functionStart)
{
	Lsda lsda;
	const uint8_t landingPadEncoding = *at++;
	lsda.landingPadBase = landingPadEncoding == kOmit ? functionStart : read_encoded(&at, landingPadEncoding);
	lsda.typeEncoding = *at++;
	lsda.typeTable = NULL;
	if (lsda.typeEncoding != kOmit)
	{
		const uintptr_t offset = read_uleb128(&at);
		lsda.typeTable = at + offset;
	}
	lsda.callSiteEncoding = *at++;
	const uintptr_t length = read_uleb128(&at);
	lsda.callSites = at;
	lsda.actions = at + length;
	return lsda;
}

// The handler clause's TypeDescriptor for type filter `filter` (null: catch (...)).
static const TypeDescriptor* clause_type(const Lsda* lsda, intptr_t filter)
{
	const uint8_t* entry = lsda->typeTable - (size_t)filter * encoded_size(lsda->typeEncoding);
	return (const TypeDescriptor*)read_encoded(&entry, lsda->typeEncoding);
}

typedef struct Landing
{
	uintptr_t pad;      // 0: none in this frame for the call
	intptr_t selector;  // the matching clause's filter, 0 for a cleanup
	int handles;        // a clause takes the exception
	int cleans;         // a cleanup runs
} Landing;

// What the frame does with the exception raised at `ip`: its landing pad, and whether a clause takes it.
static Landing find_landing(const Lsda* lsda, uintptr_t ip, uintptr_t functionStart, const C4Exception* exception, int catches)
{
	Landing landing = { 0, 0, 0, 0 };
	const uint8_t* at = lsda->callSites;
	while (at < lsda->actions)
	{
		const uintptr_t start = read_encoded(&at, lsda->callSiteEncoding);
		const uintptr_t length = read_encoded(&at, lsda->callSiteEncoding);
		const uintptr_t pad = read_encoded(&at, lsda->callSiteEncoding);
		const uintptr_t action = read_uleb128(&at);
		if (ip < functionStart + start) break; // the records are in order: none holds ip
		if (ip >= functionStart + start + length) continue;
		if (!pad) return landing;
		landing.pad = lsda->landingPadBase + pad;
		if (!action)
		{
			landing.cleans = 1;
			return landing;
		}
		const uint8_t* record = lsda->actions + action - 1;
		for (;;)
		{
			const intptr_t filter = read_sleb128(&record);
			const uint8_t* next = record;
			const intptr_t offset = read_sleb128(&record);
			if (filter == 0) landing.cleans = 1;
			else if (filter > 0 && catches)
			{
				const TypeDescriptor* type = clause_type(lsda, filter);
				if (!type || (exception && catchable_as(exception, type)))
				{
					landing.handles = 1;
					landing.selector = filter;
					return landing;
				}
			}
			if (!offset) return landing;
			record = next + offset;
		}
	}
	return landing;
}

_Unwind_Reason_Code __c4_personality_v0(int version, _Unwind_Action actions, uint64_t exceptionClass,
	struct _Unwind_Exception* unwind, struct _Unwind_Context* context)
{
	(void)exceptionClass;
	if (version != 1) return _URC_FATAL_PHASE1_ERROR;
	const uint8_t* table = (const uint8_t*)_Unwind_GetLanguageSpecificData(context);
	if (!table) return _URC_CONTINUE_UNWIND;
	int beforeInstruction = 0;
	uintptr_t ip = _Unwind_GetIPInfo(context, &beforeInstruction);
	if (!beforeInstruction) ip -= 1; // the call's own bytes, not the next instruction's
	const uintptr_t functionStart = _Unwind_GetRegionStart(context);
	const Lsda lsda = read_header(table, functionStart);
	// A forced unwind (a thread's cancellation) runs cleanups and nothing catches it; a foreign exception only
	// `catch (...)` takes.
	const int catches = !(actions & _UA_FORCE_UNWIND);
	const Landing landing = find_landing(&lsda, ip, functionStart, c4_exception_of(unwind), catches);
	if (actions & _UA_SEARCH_PHASE) return landing.handles ? _URC_HANDLER_FOUND : _URC_CONTINUE_UNWIND;
	const int install = (actions & _UA_HANDLER_FRAME) ? landing.handles : landing.cleans;
	if (!landing.pad || !install) return _URC_CONTINUE_UNWIND;
	_Unwind_SetGR(context, __builtin_eh_return_data_regno(0), (uintptr_t)unwind);
	_Unwind_SetGR(context, __builtin_eh_return_data_regno(1), (uintptr_t)((actions & _UA_HANDLER_FRAME) ? landing.selector : 0));
	_Unwind_SetIP(context, landing.pad);
	return _URC_INSTALL_CONTEXT;
}
