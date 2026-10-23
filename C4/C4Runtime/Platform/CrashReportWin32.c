// Crash reports (PlatformRuntimeC.h). A report is "C4: fatal: <what>", the thread, and the stack that reached the
// error, a frame a line, named from the PDBs of the process's modules through DbgHelp (loaded only when a report is
// written). It goes to standard error and, once a program named a crash log, is appended there after the time and the
// process id. With a crash log named, faults nothing handles and bad arguments to the C runtime are reported too.
//
// The stack is taken on the failing thread; the names are found and the text written by a thread of its own, whose
// stack is whole even when the failing one's is not. The failing thread waits for it a while: a fatal error of the
// collector comes while it has every other thread stopped, and one of those may hold a lock DbgHelp needs (the
// loader's, the heap's). When the writer does not finish, the report goes out unnamed, the addresses alone.
//
// c4_stack_text names the same frames for a report of the program's own; in a catch handler, the stack of the throw.

#include "PlatformRuntimeC.h"
#include "ThreadRuntimeC.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum { kFrames = 62, kLogUnits = 1024, kTextBytes = 32768, kNameBytes = 512, kWriterWaitMs = 15000 };
// UnDecorateSymbolName for a type's name: UNDNAME_32_BIT_DECODE | UNDNAME_TYPE_ONLY (not every SDK names the second).
enum { kUndecorateTypeName = 0x0800 | 0x2000 };

typedef DWORD(WINAPI* SetOptionsFn)(DWORD);
typedef BOOL(WINAPI* InitializeFn)(HANDLE, PCWSTR, BOOL);
typedef BOOL(WINAPI* FromAddressFn)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFOW);
typedef BOOL(WINAPI* LineFromAddressFn)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINEW64);

// What a report says: the error, the thread it happened on, and that thread's stack. Frames are return addresses,
// looked up a byte before (the call); a fault's own instruction is held a byte after.
typedef struct Report
{
	char what[kNameBytes];
	DWORD thread;
	char threadName[kNameBytes];
	void* frames[kFrames];
	USHORT count;
} Report;

// A report's text: it goes out in one write, so a crash log never holds half of one.
typedef struct Text
{
	char bytes[kTextBytes];
	size_t length;
} Text;

typedef struct Symbols
{
	HANDLE session;
	FromAddressFn fromAddress;
	LineFromAddressFn lineFromAddress;
} Symbols;

// The crash log (UTF-16, empty: none); one report at a time, its text named and unnamed (the writer may still hold
// the first), and the thread writing a fatal one (a second fatal error on it, from code exit runs, ends the process at
// once); the filter faults went to before.
static wchar_t g_log[kLogUnits];
static SRWLOCK g_reporting = SRWLOCK_INIT;
static SRWLOCK g_naming = SRWLOCK_INIT;
static Report g_report;
static Text g_named;
static Text g_unnamed;
static volatile DWORD g_fatalThread;
static LPTOP_LEVEL_EXCEPTION_FILTER g_previous;
static volatile LONG g_filterInstalled;

static void Append(Text* text, const char* format, ...)
{
	if (text->length >= sizeof(text->bytes) - 1) return;
	va_list arguments;
	va_start(arguments, format);
	const int written = vsnprintf(text->bytes + text->length, sizeof(text->bytes) - text->length, format, arguments);
	va_end(arguments);
	if (written > 0) text->length += (size_t)written;
	if (text->length > sizeof(text->bytes) - 1) text->length = sizeof(text->bytes) - 1;
}

// A DbgHelp session of its own, on a duplicate of the process handle (SourceLinesWin32.c keeps another). It reads the
// PDBs the modules name and those beside them, never a symbol server _NT_SYMBOL_PATH may name.
static Symbols OpenSymbols(void)
{
	Symbols symbols = { NULL, NULL, NULL };
	HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!dbghelp) return symbols;
	const SetOptionsFn setOptions = (SetOptionsFn)(void*)GetProcAddress(dbghelp, "SymSetOptions");
	const InitializeFn initialize = (InitializeFn)(void*)GetProcAddress(dbghelp, "SymInitializeW");
	symbols.fromAddress = (FromAddressFn)(void*)GetProcAddress(dbghelp, "SymFromAddrW");
	symbols.lineFromAddress = (LineFromAddressFn)(void*)GetProcAddress(dbghelp, "SymGetLineFromAddrW64");
	HANDLE session = NULL;
	if (!setOptions || !initialize || !symbols.fromAddress ||
		!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &session, 0, FALSE, DUPLICATE_SAME_ACCESS))
		return symbols;
	setOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS |
		SYMOPT_IGNORE_NT_SYMPATH);
	if (initialize(session, NULL, TRUE)) symbols.session = session;
	else CloseHandle(session);
	return symbols;
}

static void AppendHeader(Text* text, const Report* report)
{
	Append(text, "C4: fatal: %s\n", report->what);
	Append(text, "    thread %lu", (unsigned long)report->thread);
	if (report->threadName[0]) Append(text, " \"%s\"", report->threadName);
	Append(text, "\n");
}

// "name+0x12 (file:line)", or "module.exe+0x1234" when the module has no PDB.
static void AppendFrame(Text* text, const Symbols* symbols, void* frame)
{
	const DWORD64 address = (DWORD64)(uintptr_t)frame;
	const DWORD64 call = address - 1;
	union
	{
		SYMBOL_INFOW info;
		char room[sizeof(SYMBOL_INFOW) + kNameBytes * sizeof(wchar_t)];
	} symbol;
	memset(&symbol, 0, sizeof(symbol));
	symbol.info.SizeOfStruct = sizeof(SYMBOL_INFOW);
	symbol.info.MaxNameLen = kNameBytes;
	DWORD64 displacement = 0;
	if (symbols->session && symbols->fromAddress(symbols->session, call, &displacement, &symbol.info))
	{
		Append(text, "    at %ls+0x%llx", symbol.info.Name, (unsigned long long)(displacement + 1));
		IMAGEHLP_LINEW64 line;
		memset(&line, 0, sizeof(line));
		line.SizeOfStruct = sizeof(line);
		DWORD column = 0;
		if (symbols->lineFromAddress && symbols->lineFromAddress(symbols->session, call, &column, &line))
			Append(text, " (%ls:%lu)", line.FileName, (unsigned long)line.LineNumber);
		Append(text, "\n");
		return;
	}
	HMODULE module = NULL;
	wchar_t path[MAX_PATH] = L"?";
	const DWORD byAddress = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
	if (GetModuleHandleExW(byAddress, (LPCWSTR)(uintptr_t)call, &module))
		GetModuleFileNameW(module, path, MAX_PATH);
	const wchar_t* name = wcsrchr(path, L'\\');
	Append(text, "    at %ls+0x%llx\n", name ? name + 1 : path, (unsigned long long)(address - (DWORD64)(uintptr_t)module));
}

static void WriteOut(const Text* text)
{
	HANDLE errors = GetStdHandle(STD_ERROR_HANDLE);
	DWORD written = 0;
	if (errors && errors != INVALID_HANDLE_VALUE) WriteFile(errors, text->bytes, (DWORD)text->length, &written, NULL);
	if (!g_log[0]) return;
	HANDLE file = CreateFileW(g_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) return;
	SYSTEMTIME now;
	GetLocalTime(&now);
	char stamp[96];
	const int length = snprintf(stamp, sizeof(stamp), "%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] ", now.wYear, now.wMonth, now.wDay,
		now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, (unsigned long)GetCurrentProcessId());
	if (length > 0) WriteFile(file, stamp, (DWORD)length, &written, NULL);
	WriteFile(file, text->bytes, (DWORD)text->length, &written, NULL);
	CloseHandle(file);
}

// The process's session, opened the first time a stack is named and kept (reading the PDBs is the slow part); DbgHelp
// is single-threaded, so it is used under g_naming.
static Symbols* NamingSymbols(void)
{
	static Symbols symbols;
	static int opened;
	if (!opened)
	{
		opened = 1;
		symbols = OpenSymbols();
	}
	return &symbols;
}

static void AppendFrames(Text* text, void* const* frames, USHORT count)
{
	AcquireSRWLockExclusive(&g_naming);
	Symbols* symbols = NamingSymbols();
	for (USHORT i = 0; i < count; ++i) AppendFrame(text, symbols, frames[i]);
	ReleaseSRWLockExclusive(&g_naming);
}

static DWORD WINAPI WriteNamed(void* parameter)
{
	const Report* report = (const Report*)parameter;
	g_named.length = 0;
	AppendHeader(&g_named, report);
	AppendFrames(&g_named, report->frames, report->count);
	fflush(stderr);
	WriteOut(&g_named);
	return 0;
}

static void WriteUnnamed(const Report* report)
{
	g_unnamed.length = 0;
	AppendHeader(&g_unnamed, report);
	for (USHORT i = 0; i < report->count; ++i) Append(&g_unnamed, "    at 0x%llx\n", (unsigned long long)(uintptr_t)report->frames[i]);
	WriteOut(&g_unnamed);
}

static void Deliver(Report* report)
{
	HANDLE writer = CreateThread(NULL, 0, WriteNamed, report, 0, NULL);
	const bool finished = writer && WaitForSingleObject(writer, kWriterWaitMs) == WAIT_OBJECT_0;
	if (writer) CloseHandle(writer);
	if (!finished) WriteUnnamed(report);
}

static void Describe(Report* report, const char* what)
{
	snprintf(report->what, sizeof(report->what), "%s", what);
	report->thread = GetCurrentThreadId();
	report->threadName[0] = 0;
	c4_thread_name(report->threadName, (int32_t)sizeof(report->threadName));
}

// The calling thread's stack, the frames of this function and the `skip` above it (the reporting code's) left out.
__attribute__((noinline)) static void Capture(Report* report, ULONG skip)
{
	report->count = RtlCaptureStackBackTrace(skip + 1, kFrames, report->frames, NULL);
}

// A fault's stack, from the faulting instruction outward, as its context left it.
static void Unwind(Report* report, const CONTEXT* fault)
{
	CONTEXT context = *fault;
	ULONG_PTR low = 0;
	ULONG_PTR high = 0;
	GetCurrentThreadStackLimits(&low, &high);
	report->count = 0;
	while (report->count < kFrames && context.Rip)
	{
		report->frames[report->count] = (void*)(uintptr_t)(report->count == 0 ? context.Rip + 1 : context.Rip);
		report->count++;
		DWORD64 imageBase = 0;
		PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, NULL);
		if (function)
		{
			void* handlerData = NULL;
			DWORD64 establisher = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, NULL);
		}
		else if (context.Rsp >= low && context.Rsp + sizeof(DWORD64) <= high)
		{
			// A leaf function: its return address is on top of the stack.
			context.Rip = *(const DWORD64*)(uintptr_t)context.Rsp;
			context.Rsp += sizeof(DWORD64);
		}
		else break;
		if (context.Rsp < low || context.Rsp >= high) break;
	}
}

// The C++ runtime's record of the exception a catch handler is handling, and of the processor's state at its throw
// (vcruntime: what std::current_exception and a rethrow read); null outside a handler.
void** __current_exception(void);
void** __current_exception_context(void);

// A thrown C++ object's type as its TypeDescriptor names it (".?AVRpcError@Ied@@"), undecorated
// ("class Ied::RpcError"); empty when the record is not a C++ exception's.
static void AppendThrownType(Text* text, const EXCEPTION_RECORD* record)
{
	if (record->ExceptionCode != 0xE06D7363 || record->NumberParameters < 4) return;
	const uint8_t* imageBase = (const uint8_t*)record->ExceptionInformation[3];
	const int32_t* throwInfo = (const int32_t*)record->ExceptionInformation[2];
	if (!throwInfo) return;
	const int32_t* catchable = (const int32_t*)(imageBase + throwInfo[3]);
	const int32_t* first = (const int32_t*)(imageBase + catchable[1]);
	const char* decorated = (const char*)(imageBase + first[1] + 2 * sizeof(void*));
	typedef DWORD(WINAPI* UndecorateFn)(PCSTR, PSTR, DWORD, DWORD);
	HMODULE dbghelp = GetModuleHandleW(L"dbghelp.dll");
	const UndecorateFn undecorate = dbghelp ? (UndecorateFn)(void*)GetProcAddress(dbghelp, "UnDecorateSymbolName") : NULL;
	char name[kNameBytes];
	if (decorated[0] == '.' && undecorate && undecorate(decorated + 1, name, sizeof(name), kUndecorateTypeName))
		Append(text, "    thrown: %s\n", name);
	else Append(text, "    thrown: %s\n", decorated);
}

__attribute__((noinline)) int64_t c4_stack_text(char* out, int64_t capacity, int32_t skip)
{
	if (!out || capacity <= 0) return 0;
	Report* report = (Report*)malloc(sizeof(Report));
	Text* text = (Text*)malloc(sizeof(Text));
	int64_t length = 0;
	if (report && text)
	{
		text->length = 0;
		const EXCEPTION_RECORD* handling = (const EXCEPTION_RECORD*)*__current_exception();
		const CONTEXT* thrown = (const CONTEXT*)*__current_exception_context();
		if (handling && thrown)
		{
			AcquireSRWLockExclusive(&g_naming);
			NamingSymbols();
			AppendThrownType(text, handling);
			ReleaseSRWLockExclusive(&g_naming);
			Unwind(report, thrown);
		}
		else Capture(report, skip < 0 ? 1u : (ULONG)skip + 1u);
		AppendFrames(text, report->frames, report->count);
		length = (int64_t)text->length < capacity - 1 ? (int64_t)text->length : capacity - 1;
		memcpy(out, text->bytes, (size_t)length);
	}
	out[length] = 0;
	free(report);
	free(text);
	return length;
}

__attribute__((noinline)) void c4_fatal_report(const char* what)
{
	if (g_fatalThread == GetCurrentThreadId()) _Exit(1);
	AcquireSRWLockExclusive(&g_reporting);
	g_fatalThread = GetCurrentThreadId();
	Describe(&g_report, what);
	Capture(&g_report, 1);
	Deliver(&g_report);
}

__attribute__((noreturn)) void c4_fatal(const char* what)
{
	c4_fatal_report(what);
	exit(1);
}

static const char* FaultName(DWORD code)
{
	switch (code)
	{
	case EXCEPTION_ACCESS_VIOLATION: return "an access violation";
	case EXCEPTION_STACK_OVERFLOW: return "a stack overflow";
	case EXCEPTION_INT_DIVIDE_BY_ZERO: return "an integer division by zero";
	case EXCEPTION_ILLEGAL_INSTRUCTION: return "an illegal instruction";
	case EXCEPTION_IN_PAGE_ERROR: return "a page that could not be read in";
	case EXCEPTION_DATATYPE_MISALIGNMENT: return "a misaligned access";
	case EXCEPTION_PRIV_INSTRUCTION: return "a privileged instruction";
	default: return "a fault";
	}
}

static void DescribeFault(Report* report, const EXCEPTION_RECORD* record)
{
	char what[kNameBytes];
	const DWORD code = record->ExceptionCode;
	if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
	{
		const ULONG_PTR kind = record->ExceptionInformation[0];
		snprintf(what, sizeof(what), "%s (0x%08lX) %s address 0x%llx", FaultName(code), (unsigned long)code,
			kind == 1 ? "writing" : kind == 8 ? "executing" : "reading", (unsigned long long)record->ExceptionInformation[1]);
	}
	else snprintf(what, sizeof(what), "%s (0x%08lX)", FaultName(code), (unsigned long)code);
	Describe(report, what);
}

// A C++ exception goes on to the C runtime's filter, whose terminate reports it; any other fault is reported here, then
// handed on, so the system still ends the process with the fault's code (and Windows Error Reporting sees it).
static LONG WINAPI OnUnhandledFault(EXCEPTION_POINTERS* fault)
{
	if (fault->ExceptionRecord->ExceptionCode != 0xE06D7363 && TryAcquireSRWLockExclusive(&g_reporting))
	{
		DescribeFault(&g_report, fault->ExceptionRecord);
		Unwind(&g_report, fault->ContextRecord);
		Deliver(&g_report);
		ReleaseSRWLockExclusive(&g_reporting);
	}
	return g_previous ? g_previous(fault) : EXCEPTION_CONTINUE_SEARCH;
}

static void OnInvalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t reserved)
{
	(void)expression;
	(void)function;
	(void)file;
	(void)line;
	(void)reserved;
	c4_fatal("a C runtime function was called with an invalid argument");
}

void c4_crash_log(const char* path)
{
	AcquireSRWLockExclusive(&g_reporting);
	g_log[0] = 0;
	if (path && path[0] && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, g_log, kLogUnits) <= 0) g_log[0] = 0;
	ReleaseSRWLockExclusive(&g_reporting);
	if (!g_log[0] || InterlockedExchange(&g_filterInstalled, 1)) return;
	g_previous = SetUnhandledExceptionFilter(OnUnhandledFault);
	_set_invalid_parameter_handler(OnInvalidParameter);
}
