// Guarded calls into script code (docs/runtime/04 section 7): a hardware fault stops the call and is reported, so a
// crashing script does not take the program down. It lives here, not in the C4 runtime: the runtime is compiled with
// -mstackrealign, under which clang miscompiles SEH __try / __except.

#include "C4Script.h"
#include <malloc.h>
#include <windows.h>

namespace
{
	bool IsFault(DWORD code)
	{
		switch (code)
		{
		case EXCEPTION_ACCESS_VIOLATION:
		case EXCEPTION_IN_PAGE_ERROR:
		case EXCEPTION_ILLEGAL_INSTRUCTION:
		case EXCEPTION_PRIV_INSTRUCTION:
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
		case EXCEPTION_INT_OVERFLOW:
		case EXCEPTION_STACK_OVERFLOW:
		case EXCEPTION_DATATYPE_MISALIGNMENT:
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		case EXCEPTION_FLT_INVALID_OPERATION:
		case EXCEPTION_FLT_OVERFLOW:
		case EXCEPTION_FLT_UNDERFLOW:
		case EXCEPTION_FLT_STACK_CHECK:
		case EXCEPTION_FLT_DENORMAL_OPERAND:
		case EXCEPTION_FLT_INEXACT_RESULT:
			return true;
		default:
			return false;
		}
	}

	// The __except filter: a fault is handled and described; anything else (a C++ or C4 exception) is not ours.
	int Describe(const EXCEPTION_POINTERS* info, c4s_fault* fault)
	{
		const EXCEPTION_RECORD* record = info->ExceptionRecord;
		if (!IsFault(record->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;
		fault->code = record->ExceptionCode;
		fault->address = reinterpret_cast<uint64_t>(record->ExceptionAddress);
		const bool touchedMemory = record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR;
		if (touchedMemory && record->NumberParameters >= 2)
		{
			fault->write = static_cast<uint32_t>(record->ExceptionInformation[0]);
			fault->target = static_cast<uint64_t>(record->ExceptionInformation[1]);
		}
		return EXCEPTION_EXECUTE_HANDLER;
	}
}

int32_t c4s_guarded_call(void (*fn)(void*), void* context, c4s_fault* fault)
{
	c4s_fault seen = {};
	__try
	{
		fn(context);
	}
	__except (Describe(GetExceptionInformation(), &seen))
	{
		if (seen.code == EXCEPTION_STACK_OVERFLOW) _resetstkoflw(); // the guard page, for the next overflow
		if (fault) *fault = seen;
		return 0;
	}
	return 1;
}
