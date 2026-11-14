// Crash reports (PlatformRuntimeC.h) on Linux. A report is "C4: fatal: <what>", the thread, and the stack that reached
// the error, a frame a line, named from the ELF files of the process's modules (ModulesLinux.h): the function from the
// symbol table, the file and line from the DWARF line tables when the module has them. It goes to standard error and,
// once a program named a crash log, is appended there after the time and the process id. With a crash log named, or in
// a test run (c4_report_faults), faults nothing handles are reported too - SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT - on
// a stack of the thread's own (c4_crash_attach_thread), so a stack overflow is reported; the signal then goes on to the
// action it had before, so the system still ends the process with it.
//
// A fault is reported in its signal handler, so a report is made with async-signal-safe calls only: the text is
// formatted here, and the names are read from the files with system calls into static memory, under no lock. The one
// exception is the unwinder, which takes the loader's lock (dl_iterate_phdr) as every throw does. Windows names the
// frames on a thread of its own and gives up after a while, because DbgHelp takes locks a stopped thread may hold;
// nothing here takes one, so the failing thread names them itself.
//
// Unlike Windows: glibc has no handler for C runtime functions given invalid arguments (its checks abort, which is
// reported); and a fault's code is its signal. In a catch handler the stack of the throw is gone (the unwinder runs a
// handler after unwinding to it), so the exception runtime records it at the throw (c4_eh_handling, ExceptionsPosix.c)
// and c4_stack_text gives it with the thrown type, as Windows does.

#include "PlatformRuntimeC.h"
#include "ThreadRuntimeC.h"
#include "ModulesLinux.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>

enum { kFrames = 62, kTextBytes = 32768, kNameBytes = 512, kModuleNameBytes = 256, kFrameTextBytes = 768, kStampBytes = 96 };
enum { kLogBytes = 4096, kFaultStackBytes = 65536, kWaitForReportMs = 15000, kNanosecondsPerMillisecond = 1000000 };
// A fault this close to the stack pointer is a stack overflow: the pages around it are the stack's, mapped, or its guard.
enum { kOverflowReach = 65536 };
// The x64 page fault (trap 14) and the bits of its error code that say what the access was.
enum { kPageFaultTrap = 14, kPageFaultWrite = 0x2, kPageFaultFetch = 0x10 };
enum { kSecondsPerMinute = 60, kSecondsPerHour = 3600, kSecondsPerDay = 86400 };
// Where /proc/self/maps writes a mapping's read and execute rights.
enum { kReadRight = 0, kExecuteRight = 2 };

static const int kFaultSignals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
enum { kFaultSignalCount = sizeof(kFaultSignals) / sizeof(kFaultSignals[0]) };

// --- text --------------------------------------------------------------------------------------------------

// Text written into a buffer, cut at its end, always terminated.
typedef struct Writer
{
	char* bytes;
	size_t capacity;
	size_t length;
} Writer;

static Writer WriterOf(char* bytes, size_t capacity)
{
	Writer writer = { bytes, capacity, 0 };
	bytes[0] = 0;
	return writer;
}

static void AppendBytes(Writer* writer, const char* bytes, size_t length)
{
	const size_t room = writer->capacity - 1 - writer->length;
	if (length > room) length = room;
	memcpy(writer->bytes + writer->length, bytes, length);
	writer->length += length;
	writer->bytes[writer->length] = 0;
}

static void Append(Writer* writer, const char* text) { AppendBytes(writer, text, strlen(text)); }

static void AppendNumber(Writer* writer, uint64_t value, uint32_t base, uint32_t leastDigits)
{
	char digits[sizeof(uint64_t) * 8];
	uint32_t count = 0;
	do
	{
		digits[count++] = "0123456789abcdef"[value % base];
		value /= base;
	} while (value != 0 && count < sizeof(digits));
	while (count < leastDigits && count < sizeof(digits)) digits[count++] = '0';
	char ordered[sizeof(digits)];
	for (uint32_t i = 0; i < count; ++i) ordered[i] = digits[count - 1 - i];
	AppendBytes(writer, ordered, count);
}

static void AppendHex(Writer* writer, uint64_t value)
{
	Append(writer, "0x");
	AppendNumber(writer, value, 16, 1);
}

// --- what a report says ------------------------------------------------------------------------------------

// The error, the thread it happened on, and that thread's stack. Frames are return addresses, looked up a byte before
// (the call); a fault's own instruction is held a byte after.
typedef struct Report
{
	char what[kNameBytes];
	uint64_t thread;
	char threadName[kNameBytes];
	void* frames[kFrames];
	int32_t count;
} Report;

// A frame on its way to its line of a report: where it is, then its text.
typedef struct Frame
{
	uintptr_t address;
	bool mapped;
	c4_linux_mapping mapping;      // the mapping holding it, its path not kept
	uint64_t pathHash;
	char module[kModuleNameBytes];
	const char* function;          // in the module's mapped file while the module is named
	uint64_t displacement;
	uint64_t moduleOffset;
	int32_t line;                  // in Naming.lines; -1 for none
	bool named;
	char text[kFrameTextBytes];
} Frame;

// What naming a stack needs: frames are named a module at a time, its file mapped once.
typedef struct Naming
{
	Frame frames[kFrames];
	int32_t count;
	char path[kLinuxPathBytes];
	c4_linux_line lines[kFrames];
	int32_t lineFrames[kFrames];   // the frame each line is asked for
} Naming;

static uintptr_t CallOf(const Frame* frame) { return frame->address - 1; }

static uint64_t HashText(const char* text)
{
	uint64_t hash = 14695981039346656037ull;
	for (; *text; ++text) hash = (hash ^ (unsigned char)*text) * 1099511628211ull;
	return hash;
}

static const char* BaseName(const char* path)
{
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static void PrepareFrames(Naming* naming, void* const* frames, int32_t count)
{
	naming->count = count;
	for (int32_t i = 0; i < count; ++i)
	{
		Frame* frame = &naming->frames[i];
		memset(frame, 0, sizeof(*frame));
		frame->address = (uintptr_t)frames[i];
		frame->line = -1;
	}
}

// Visits each mapping once, for every frame in it.
static bool PlaceFrames(const c4_linux_mapping* mapping, void* state)
{
	Naming* naming = (Naming*)state;
	for (int32_t i = 0; i < naming->count; ++i)
	{
		Frame* frame = &naming->frames[i];
		if (frame->mapped || CallOf(frame) < mapping->start || CallOf(frame) >= mapping->end) continue;
		frame->mapped = true;
		frame->mapping = *mapping;
		frame->mapping.path = NULL;
		frame->pathHash = HashText(mapping->path);
		Writer module = WriterOf(frame->module, sizeof(frame->module));
		Append(&module, mapping->path[0] ? BaseName(mapping->path) : "?");
	}
	return true;
}

static bool SameModule(const Frame* a, const Frame* b)
{
	return a->mapped && b->mapped && a->mapping.inode == b->mapping.inode && a->pathHash == b->pathHash;
}

// "    at Ns_Function+0x4d (/src/a.cf:42)", or the module and the offset in it ("    at libc.so.6+0x1234") when no
// symbol holds the frame, or the address alone after "?+" when no file does.
static void Render(Frame* frame, const c4_linux_line* line)
{
	Writer text = WriterOf(frame->text, sizeof(frame->text));
	Append(&text, "    at ");
	Append(&text, frame->function ? frame->function : frame->mapped ? frame->module : "?");
	Append(&text, "+");
	AppendHex(&text, frame->function ? frame->displacement : frame->mapped ? frame->moduleOffset : frame->address);
	char file[kLinuxPathBytes];
	if (line && line->name && c4_linux_line_path(line, file, (int32_t)sizeof(file)) > 0)
	{
		Append(&text, " (");
		Append(&text, file);
		Append(&text, ":");
		AppendNumber(&text, (uint64_t)line->line, 10, 1);
		Append(&text, ")");
	}
	Append(&text, "\n");
	frame->named = true;
}

// A frame's function and where it is in its module; its line asked for with the module's others.
static void Locate(Naming* naming, int32_t index, const c4_linux_elf* elf, int32_t* asked)
{
	Frame* frame = &naming->frames[index];
	uintptr_t bias = 0;
	frame->moduleOffset = frame->address - frame->mapping.start;
	if (!elf || !c4_linux_elf_load_bias(elf, &frame->mapping, &bias)) return;
	uint64_t start = 0;
	frame->function = c4_linux_elf_function(elf, CallOf(frame) - bias, &start);
	frame->displacement = frame->address - bias - start;
	frame->moduleOffset = frame->address - bias;
	naming->lines[*asked].address = CallOf(frame) - bias;
	naming->lineFrames[*asked] = index;
	++*asked;
}

// The line questions by address (each keeping its frame), as c4_linux_elf_lines takes them.
static void SortLines(Naming* naming, int32_t count)
{
	for (int32_t i = 1; i < count; ++i)
		for (int32_t j = i; j > 0 && naming->lines[j - 1].address > naming->lines[j].address; --j)
		{
			const c4_linux_line line = naming->lines[j];
			naming->lines[j] = naming->lines[j - 1];
			naming->lines[j - 1] = line;
			const int32_t frame = naming->lineFrames[j];
			naming->lineFrames[j] = naming->lineFrames[j - 1];
			naming->lineFrames[j - 1] = frame;
		}
}

// Names every frame in the module of frame `first`, its file mapped while they are.
static void NameModule(Naming* naming, int32_t first)
{
	c4_linux_mapping mapping;
	c4_linux_elf elf;
	const Frame* lead = &naming->frames[first];
	const bool opened = c4_linux_mapping_of(CallOf(lead), &mapping, naming->path) && mapping.inode != 0 &&
		c4_linux_elf_open(naming->path, mapping.inode, &elf);
	int32_t asked = 0;
	for (int32_t i = first; i < naming->count; ++i)
		if (!naming->frames[i].named && SameModule(&naming->frames[i], lead)) Locate(naming, i, opened ? &elf : NULL, &asked);
	SortLines(naming, asked);
	if (opened) c4_linux_elf_lines(&elf, naming->lines, asked);
	for (int32_t i = 0; i < asked; ++i) naming->frames[naming->lineFrames[i]].line = i;
	for (int32_t i = first; i < naming->count; ++i)
	{
		Frame* frame = &naming->frames[i];
		if (!frame->named && SameModule(frame, lead)) Render(frame, frame->line >= 0 ? &naming->lines[frame->line] : NULL);
	}
	if (opened) c4_linux_elf_close(&elf);
}

static void AppendFrames(Writer* text, Naming* naming, void* const* frames, int32_t count)
{
	PrepareFrames(naming, frames, count);
	c4_linux_each_mapping(PlaceFrames, naming);
	for (int32_t i = 0; i < count; ++i)
	{
		Frame* frame = &naming->frames[i];
		if (frame->named) continue;
		if (frame->mapped) NameModule(naming, i);
		else Render(frame, NULL);
	}
	for (int32_t i = 0; i < count; ++i) Append(text, naming->frames[i].text);
}

static void AppendHeader(Writer* text, const Report* report)
{
	Append(text, "C4: fatal: ");
	Append(text, report->what);
	Append(text, "\n    thread ");
	AppendNumber(text, report->thread, 10, 1);
	if (report->threadName[0])
	{
		Append(text, " \"");
		Append(text, report->threadName);
		Append(text, "\"");
	}
	Append(text, "\n");
}

// --- capturing a stack -------------------------------------------------------------------------------------

typedef struct Walk
{
	Report* report;
	int32_t skip;           // frames still to leave out
	uintptr_t faultPc;      // a fault's: the frames before its own are the handler's (0 for no fault)
	bool reached;
} Walk;

static _Unwind_Reason_Code KeepFrame(struct _Unwind_Context* context, void* state)
{
	Walk* walk = (Walk*)state;
	int beforeInstruction = 0;
	const uintptr_t ip = (uintptr_t)_Unwind_GetIPInfo(context, &beforeInstruction);
	if (!walk->reached && !(beforeInstruction && ip == walk->faultPc)) return _URC_NO_REASON;
	walk->reached = true;
	if (walk->skip > 0)
	{
		walk->skip--;
		return _URC_NO_REASON;
	}
	if (walk->report->count >= kFrames || (ip == 0 && walk->report->count > 0)) return _URC_END_OF_STACK;
	walk->report->frames[walk->report->count++] = (void*)(beforeInstruction ? ip + 1 : ip);
	return _URC_NO_REASON;
}

// The calling thread's stack, the frames of this function and the `skip` above it (the reporting code's) left out.
__attribute__((noinline)) static void Capture(Report* report, int32_t skip)
{
	Walk walk = { report, skip + 1, 0, true };
	report->count = 0;
	_Unwind_Backtrace(KeepFrame, &walk);
}

// Whether a mapping holds `address` with right `right` (0 read, 2 execute, as /proc/self/maps writes them).
static bool IsMappedWith(uintptr_t address, int32_t right)
{
	c4_linux_mapping mapping;
	char path[kLinuxPathBytes];
	return c4_linux_mapping_of(address, &mapping, path) && mapping.rights[right] != '-';
}

// A fault's stack, from the faulting instruction outward. A call to an address no code is at (a null function pointer)
// faults where nothing can be unwound: the return address the call left on top of the stack names the caller.
static void CaptureFault(Report* report, const ucontext_t* context)
{
	const uintptr_t pc = (uintptr_t)context->uc_mcontext.gregs[REG_RIP];
	Walk walk = { report, 0, pc, false };
	report->count = 0;
	_Unwind_Backtrace(KeepFrame, &walk);
	if (walk.reached && report->count > 1) return;
	report->frames[0] = (void*)(pc + 1);
	report->count = 1;
	const uintptr_t top = (uintptr_t)context->uc_mcontext.gregs[REG_RSP];
	if (IsMappedWith(pc, kExecuteRight) || !IsMappedWith(top, kReadRight)) return;
	const uintptr_t returnAddress = *(const uintptr_t*)top;
	if (IsMappedWith(returnAddress - 1, kExecuteRight)) report->frames[report->count++] = (void*)returnAddress;
}

// --- writing a report --------------------------------------------------------------------------------------

// The crash log (empty: none); the seconds its stamps add to UTC (the local offset when the log was named or a fatal
// error last reported: localtime takes a lock a fault may hold); the thread writing a report (0: none) and the thread
// writing a fatal one (a second fatal error on it, from code exit runs, ends the process at once); the actions the
// fault signals had before.
static char g_log[kLogBytes];
static volatile int32_t g_logOffsetSeconds;
static atomic_uint_fast64_t g_reportingThread;
static atomic_uint_fast64_t g_fatalThread;
static struct sigaction g_previous[kFaultSignalCount];
static atomic_int g_handlersInstalled;
// One report at a time is made here, under g_reportingThread.
static Report g_report;
static Naming g_naming;
static char g_text[kTextBytes];

static uint64_t CurrentThread(void) { return (uint64_t)syscall(SYS_gettid); }

static void Pause(int64_t nanoseconds)
{
	struct timespec pause = { 0, (long)nanoseconds };
	nanosleep(&pause, NULL);
}

static bool TryAcquire(uint64_t self)
{
	uint_fast64_t none = 0;
	return atomic_compare_exchange_strong(&g_reportingThread, &none, self);
}

static void Acquire(uint64_t self)
{
	while (!TryAcquire(self)) Pause(kNanosecondsPerMillisecond);
}

// For a fault: another thread's report is waited for a while (the process ends once this one hands its fault on);
// a fault in this thread's own report is not reported.
static bool AcquireForFault(uint64_t self)
{
	for (int32_t waited = 0;; ++waited)
	{
		if (TryAcquire(self)) return true;
		if (atomic_load(&g_reportingThread) == self || waited >= kWaitForReportMs) return false;
		Pause(kNanosecondsPerMillisecond);
	}
}

static void Release(void) { atomic_store(&g_reportingThread, 0); }

static void WriteAll(int file, const char* bytes, size_t length)
{
	while (length > 0)
	{
		const ssize_t written = write(file, bytes, length);
		if (written < 0 && errno == EINTR) continue;
		if (written <= 0) return;
		bytes += written;
		length -= (size_t)written;
	}
}

static int64_t FloorDivide(int64_t value, int64_t divisor)
{
	const int64_t quotient = value / divisor;
	return quotient * divisor > value ? quotient - 1 : quotient;
}

// The Gregorian date `days` after 1970-01-01, counted in years that start on March 1 so the leap day ends a year
// (H. Hinnant, "chrono-Compatible Low-Level Date Algorithms", as Bedrock's Platform/time.cf counts).
static void CivilFromDays(int64_t days, int64_t* year, int64_t* month, int64_t* day)
{
	enum { kYearsPerEra = 400, kDaysPerEra = 146097, kDaysPerYear = 365, kDaysPerFourYears = 1461, kDaysPerCentury = 36524 };
	enum { kDaysFromMarchYearZeroToEpoch = 719468, kDaysPerFiveMonths = 153, kMonthsPerFiveMonths = 5, kMarch = 3, kMonthsPerYear = 12 };
	const int64_t shifted = days + kDaysFromMarchYearZeroToEpoch;
	const int64_t era = FloorDivide(shifted, kDaysPerEra);
	const int64_t dayOfEra = shifted - era * kDaysPerEra;
	const int64_t yearOfEra = (dayOfEra - dayOfEra / (kDaysPerFourYears - 1) + dayOfEra / kDaysPerCentury - dayOfEra / (kDaysPerEra - 1)) / kDaysPerYear;
	const int64_t dayOfYear = dayOfEra - (kDaysPerYear * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
	const int64_t marchMonth = (kMonthsPerFiveMonths * dayOfYear + 2) / kDaysPerFiveMonths;
	*day = dayOfYear - (kDaysPerFiveMonths * marchMonth + 2) / kMonthsPerFiveMonths + 1;
	*month = marchMonth < kMonthsPerYear - kMarch + 1 ? marchMonth + kMarch : marchMonth + kMarch - kMonthsPerYear;
	*year = yearOfEra + era * kYearsPerEra + (*month < kMarch ? 1 : 0);
}

// "2026-11-13 19:17:05.123 [4242] ".
static void AppendStamp(Writer* writer)
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	const int64_t local = (int64_t)now.tv_sec + g_logOffsetSeconds;
	const int64_t days = FloorDivide(local, kSecondsPerDay);
	const int64_t second = local - days * kSecondsPerDay;
	int64_t year = 0;
	int64_t month = 0;
	int64_t day = 0;
	CivilFromDays(days, &year, &month, &day);
	AppendNumber(writer, (uint64_t)year, 10, 4);
	Append(writer, "-");
	AppendNumber(writer, (uint64_t)month, 10, 2);
	Append(writer, "-");
	AppendNumber(writer, (uint64_t)day, 10, 2);
	Append(writer, " ");
	AppendNumber(writer, (uint64_t)(second / kSecondsPerHour), 10, 2);
	Append(writer, ":");
	AppendNumber(writer, (uint64_t)(second % kSecondsPerHour / kSecondsPerMinute), 10, 2);
	Append(writer, ":");
	AppendNumber(writer, (uint64_t)(second % kSecondsPerMinute), 10, 2);
	Append(writer, ".");
	AppendNumber(writer, (uint64_t)(now.tv_nsec / kNanosecondsPerMillisecond), 10, 3);
	Append(writer, " [");
	AppendNumber(writer, (uint64_t)getpid(), 10, 1);
	Append(writer, "] ");
}

// Standard error, then the crash log in one write (the stamp and the report together), so a log never holds half of one.
static void WriteOut(const Writer* text)
{
	WriteAll(STDERR_FILENO, text->bytes, text->length);
	if (!g_log[0]) return;
	const int file = open(g_log, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0666);
	if (file < 0) return;
	char stampBytes[kStampBytes];
	Writer stamp = WriterOf(stampBytes, sizeof(stampBytes));
	AppendStamp(&stamp);
	struct iovec parts[2] = { { stamp.bytes, stamp.length }, { text->bytes, text->length } };
	while (writev(file, parts, 2) < 0 && errno == EINTR) {}
	close(file);
}

static void Deliver(const Report* report)
{
	Writer text = WriterOf(g_text, sizeof(g_text));
	AppendHeader(&text, report);
	AppendFrames(&text, &g_naming, report->frames, report->count);
	WriteOut(&text);
}

static void Describe(Report* report)
{
	report->thread = CurrentThread();
	report->threadName[0] = 0;
	c4_thread_name(report->threadName, (int32_t)sizeof(report->threadName));
}

int32_t c4_eh_handling(const char** typeName, void* const** frames); // ExceptionsPosix.c

// A thrown type as its TypeDescriptor names it (".?AVRpcError@Ied@@"), undecorated as MSVC's undecorator writes it
// ("class Ied::RpcError"); a name it does not read (a template's) as it is.
static void AppendThrownType(Writer* text, const char* decorated)
{
	const char* kind = NULL;
	if (strncmp(decorated, ".?AV", 4) == 0) kind = "class ";
	else if (strncmp(decorated, ".?AU", 4) == 0) kind = "struct ";
	const char* names = decorated + 4;
	const char* end = kind ? strstr(names, "@@") : NULL;
	Append(text, "    thrown: ");
	if (!end || memchr(names, '?', (size_t)(end - names)) || end == names)
		Append(text, decorated);
	else
	{
		// The names come innermost first, each ended by '@': written outermost first, joined by "::".
		Append(text, kind);
		for (const char* at = end; ; )
		{
			const char* start = at;
			while (start > names && start[-1] != '@') --start;
			AppendBytes(text, start, (size_t)(at - start));
			if (start == names) break;
			Append(text, "::");
			at = start - 1;
		}
	}
	Append(text, "\n");
}

__attribute__((noinline)) int64_t c4_stack_text(char* out, int64_t capacity, int32_t skip)
{
	if (!out || capacity <= 0) return 0;
	Report* report = (Report*)malloc(sizeof(Report));
	Naming* naming = (Naming*)malloc(sizeof(Naming));
	char* bytes = (char*)malloc(kTextBytes);
	int64_t length = 0;
	if (report && naming && bytes)
	{
		Writer text = WriterOf(bytes, kTextBytes);
		const char* thrown = NULL;
		void* const* thrownFrames = NULL;
		const int32_t thrownCount = c4_eh_handling(&thrown, &thrownFrames);
		if (thrownCount > 0)
		{
			if (thrown) AppendThrownType(&text, thrown);
			AppendFrames(&text, naming, thrownFrames, thrownCount);
		}
		else
		{
			Capture(report, skip < 0 ? 1 : skip + 1);
			AppendFrames(&text, naming, report->frames, report->count);
		}
		length = (int64_t)text.length < capacity - 1 ? (int64_t)text.length : capacity - 1;
		memcpy(out, text.bytes, (size_t)length);
	}
	out[length] = 0;
	free(report);
	free(naming);
	free(bytes);
	return length;
}

__attribute__((noinline)) void c4_fatal_report(const char* what)
{
	const uint64_t self = CurrentThread();
	if (atomic_load(&g_fatalThread) == self) _Exit(1);
	Acquire(self);
	atomic_store(&g_fatalThread, self);
	g_logOffsetSeconds = c4_time_local_offset_seconds(c4_time_unix_ns());
	Writer error = WriterOf(g_report.what, sizeof(g_report.what));
	Append(&error, what);
	Describe(&g_report);
	Capture(&g_report, 1);
	fflush(stderr);
	Deliver(&g_report);
}

__attribute__((noreturn)) void c4_fatal(const char* what)
{
	c4_fatal_report(what);
	exit(1);
}

// --- faults ------------------------------------------------------------------------------------------------

static bool IsStackOverflow(uintptr_t address, const ucontext_t* context)
{
	const uintptr_t stack = (uintptr_t)context->uc_mcontext.gregs[REG_RSP];
	return address + kOverflowReach >= stack && address < stack + kOverflowReach;
}

static const char* AccessKind(const ucontext_t* context)
{
	const uint64_t error = (uint64_t)context->uc_mcontext.gregs[REG_ERR];
	if (context->uc_mcontext.gregs[REG_TRAPNO] != kPageFaultTrap) return "accessing";
	if (error & kPageFaultFetch) return "executing";
	return (error & kPageFaultWrite) ? "writing" : "reading";
}

static const char* SignalName(int signal)
{
	switch (signal)
	{
	case SIGSEGV: return "SIGSEGV";
	case SIGBUS: return "SIGBUS";
	case SIGFPE: return "SIGFPE";
	case SIGILL: return "SIGILL";
	case SIGABRT: return "SIGABRT";
	default: return "a signal";
	}
}

static void DescribeHardwareFault(Writer* what, int signal, const siginfo_t* info, const ucontext_t* context)
{
	const uintptr_t address = (uintptr_t)info->si_addr;
	switch (signal)
	{
	case SIGSEGV:
		if (IsStackOverflow(address, context))
		{
			Append(what, "a stack overflow (SIGSEGV)");
			return;
		}
		Append(what, "an access violation (SIGSEGV) ");
		Append(what, AccessKind(context));
		Append(what, " address ");
		AppendHex(what, address);
		return;
	case SIGBUS:
		Append(what, info->si_code == BUS_ADRALN ? "a misaligned access (SIGBUS) at address " : "a page that could not be read in (SIGBUS) at address ");
		AppendHex(what, address);
		return;
	case SIGFPE:
		Append(what, info->si_code == FPE_INTDIV ? "an integer division by zero (SIGFPE)" : info->si_code == FPE_INTOVF ? "an integer overflow (SIGFPE)" : "a floating-point error (SIGFPE)");
		return;
	case SIGILL:
		Append(what, info->si_code == ILL_PRVOPC ? "a privileged instruction (SIGILL)" : "an illegal instruction (SIGILL)");
		return;
	default:
		Append(what, "a fault (");
		Append(what, SignalName(signal));
		Append(what, ")");
		return;
	}
}

// A signal an instruction raised is a fault; one sent (si_code <= 0: kill, tgkill, sigqueue) says so; abort's says the
// program aborted.
static void DescribeFault(Report* report, int signal, const siginfo_t* info, const ucontext_t* context)
{
	Writer what = WriterOf(report->what, sizeof(report->what));
	if (signal == SIGABRT) Append(&what, "the program aborted (SIGABRT)");
	else if (info->si_code <= 0)
	{
		Append(&what, SignalName(signal));
		Append(&what, " sent to the process");
	}
	else DescribeHardwareFault(&what, signal, info, context);
	Describe(report);
}

static int32_t SignalIndex(int signal)
{
	for (int32_t i = 0; i < kFaultSignalCount; ++i)
		if (kFaultSignals[i] == signal) return i;
	return 0;
}

// The signal goes on to the action it had before (the system's: the process ends with it). An instruction's fault comes
// again when the handler returns to it; a signal that was sent is sent again.
static void HandOn(int signal, const siginfo_t* info)
{
	sigaction(signal, &g_previous[SignalIndex(signal)], NULL);
	if (info->si_code <= 0) raise(signal);
}

static void OnFault(int signal, siginfo_t* info, void* context)
{
	const int savedErrno = errno;
	if (AcquireForFault(CurrentThread()))
	{
		DescribeFault(&g_report, signal, info, (const ucontext_t*)context);
		CaptureFault(&g_report, (const ucontext_t*)context);
		Deliver(&g_report);
		Release();
	}
	HandOn(signal, info);
	errno = savedErrno;
}

void c4_report_faults(void)
{
	if (atomic_exchange(&g_handlersInstalled, 1)) return;
	g_logOffsetSeconds = c4_time_local_offset_seconds(c4_time_unix_ns());
	c4_crash_attach_thread();
	for (int32_t i = 0; i < kFaultSignalCount; ++i)
	{
		struct sigaction action;
		memset(&action, 0, sizeof(action));
		action.sa_sigaction = OnFault;
		action.sa_flags = SA_SIGINFO | SA_ONSTACK;
		sigemptyset(&action.sa_mask);
		sigaction(kFaultSignals[i], &action, &g_previous[i]);
	}
}

void c4_crash_log(const char* path)
{
	Acquire(CurrentThread());
	g_log[0] = 0;
	if (path && path[0] && strlen(path) < sizeof(g_log)) strcpy(g_log, path);
	g_logOffsetSeconds = c4_time_local_offset_seconds(c4_time_unix_ns());
	Release();
	if (g_log[0]) c4_report_faults();
}

// --- each thread's stack for faults ------------------------------------------------------------------------

// The stack's memory, a guard page under it, is the thread's key value: the key's destructor frees it as the thread ends.
static pthread_key_t g_faultStackKey;
static pthread_once_t g_faultStackOnce = PTHREAD_ONCE_INIT;
static bool g_faultStackKeyMade;

static size_t GuardBytes(void) { return (size_t)getpagesize(); }

static void FreeFaultStack(void* memory)
{
	stack_t current;
	if (sigaltstack(NULL, &current) == 0 && current.ss_sp == (uint8_t*)memory + GuardBytes())
	{
		stack_t none;
		memset(&none, 0, sizeof(none));
		none.ss_flags = SS_DISABLE;
		sigaltstack(&none, NULL);
	}
	munmap(memory, GuardBytes() + kFaultStackBytes);
}

static void MakeFaultStackKey(void) { g_faultStackKeyMade = pthread_key_create(&g_faultStackKey, FreeFaultStack) == 0; }

void c4_crash_attach_thread(void)
{
	pthread_once(&g_faultStackOnce, MakeFaultStackKey);
	stack_t current;
	// A thread that has a signal stack (this one's, or the program's own) keeps it.
	if (!g_faultStackKeyMade || sigaltstack(NULL, &current) != 0 || !(current.ss_flags & SS_DISABLE)) return;
	uint8_t* memory = (uint8_t*)mmap(NULL, GuardBytes() + kFaultStackBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
	if (memory == MAP_FAILED) return;
	mprotect(memory, GuardBytes(), PROT_NONE);
	stack_t stack;
	memset(&stack, 0, sizeof(stack));
	stack.ss_sp = memory + GuardBytes();
	stack.ss_size = kFaultStackBytes;
	if (sigaltstack(&stack, NULL) != 0) munmap(memory, GuardBytes() + kFaultStackBytes);
	else if (pthread_setspecific(g_faultStackKey, memory) != 0) FreeFaultStack(memory);
}
