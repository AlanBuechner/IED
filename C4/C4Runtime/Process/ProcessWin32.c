// Child processes for Bedrock's Process namespace (ProcessRuntimeC.h). Only the child's ends of its pipes are
// inherited (PROC_THREAD_ATTRIBUTE_HANDLE_LIST), so two children started at once never hold each other's pipes open.
// A child starts suspended and joins a job object before it runs, so killing the job ends whatever it started too. A
// terminal is a pseudo console (ConPTY, Windows 10 1809), whose functions are looked up at run time.

#include "ProcessWin32.h"
#include "PathRuntimeC.h"
#include "SystemErrorC.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef HRESULT (WINAPI* CreatePseudoConsoleFn)(COORD, HANDLE, HANDLE, DWORD, HANDLE*);
typedef HRESULT (WINAPI* ResizePseudoConsoleFn)(HANDLE, COORD);
typedef void (WINAPI* ClosePseudoConsoleFn)(HANDLE);

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE ProcThreadAttributeValue(22, FALSE, TRUE, FALSE)   // not in every SDK
#endif

enum { kDefaultColumns = 80, kDefaultRows = 25 };

static FARPROC Kernel32(const char* name) { return GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name); }

// What CreateProcessW needs besides the text: the child's ends of its streams, or the console's ends of a terminal's
// pipes, and the attribute list naming them. The parent closes all of it once the child has started.
typedef struct Launch
{
	HANDLE childEnds[3];
	HANDLE consoleInput;
	HANDLE consoleOutput;
	LPPROC_THREAD_ATTRIBUTE_LIST attributes;
} Launch;

static void EndLaunch(Launch* launch)
{
	for (int i = 0; i < 3; ++i) c4_process_win32_close(&launch->childEnds[i]);
	c4_process_win32_close(&launch->consoleInput);
	c4_process_win32_close(&launch->consoleOutput);
	if (!launch->attributes) return;
	DeleteProcThreadAttributeList(launch->attributes);
	free(launch->attributes);
}

// --- the streams -----------------------------------------------------------------------------------------------------

static HANDLE OpenNul(bool forReading)
{
	SECURITY_ATTRIBUTES inheritable = { sizeof(inheritable), NULL, TRUE };
	HANDLE nul = CreateFileW(L"NUL", forReading ? GENERIC_READ : GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, NULL);
	return nul == INVALID_HANDLE_VALUE ? NULL : nul;
}

// An inheritable copy of `handle`; null when there is none to copy.
static HANDLE InheritableCopy(HANDLE handle)
{
	HANDLE copy = NULL;
	if (!handle || handle == INVALID_HANDLE_VALUE) return NULL;
	if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &copy, 0, TRUE, DUPLICATE_SAME_ACCESS)) return NULL;
	return copy;
}

static const DWORD kStandardHandles[3] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };

// The child's end of each standard stream as its mode asks, and the parent's end of each pipe.
static bool PrepareStreams(const c4_process_options* start, c4_process* out, Launch* launch)
{
	const int32_t modes[3] = { start->stdinMode, start->stdoutMode, start->stderrMode };
	for (int32_t stream = 0; stream < 3; ++stream)
	{
		if (modes[stream] < kProcessInherit || modes[stream] > kProcessToOutput || (modes[stream] == kProcessToOutput && stream != kProcessStderr))
		{
			SetLastError(ERROR_INVALID_PARAMETER);
			return false;
		}
		HANDLE* child = &launch->childEnds[stream];
		if (modes[stream] == kProcessPipe && !c4_process_win32_pipe(stream != kProcessStdin, (HANDLE*)&out->pipes[stream], child)) return false;
		if (modes[stream] == kProcessNull && !(*child = OpenNul(stream == kProcessStdin))) return false;
		if (modes[stream] == kProcessInherit) *child = InheritableCopy(GetStdHandle(kStandardHandles[stream]));
		if (modes[stream] == kProcessToOutput) *child = InheritableCopy(launch->childEnds[kProcessStdout]);
	}
	return true;
}

// The pseudo console a terminal runs under; the parent writes the keyboard into stdin's pipe and reads the screen
// from stdout's.
static bool PrepareConsole(const c4_process_options* start, c4_process* out, Launch* launch)
{
	CreatePseudoConsoleFn create = (CreatePseudoConsoleFn)(void*)Kernel32("CreatePseudoConsole");
	if (!create)
	{
		SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
		return false;
	}
	if (!c4_process_win32_pipe(false, (HANDLE*)&out->pipes[kProcessStdin], &launch->consoleInput)) return false;
	if (!c4_process_win32_pipe(true, (HANDLE*)&out->pipes[kProcessStdout], &launch->consoleOutput)) return false;
	const COORD size = { (SHORT)(start->columns > 0 ? start->columns : kDefaultColumns), (SHORT)(start->rows > 0 ? start->rows : kDefaultRows) };
	HANDLE console = NULL;
	const HRESULT made = create(size, launch->consoleInput, launch->consoleOutput, 0, &console);
	if (FAILED(made))
	{
		SetLastError(HRESULT_CODE(made));
		return false;
	}
	out->console = console;
	return true;
}

// --- starting --------------------------------------------------------------------------------------------------------

static HANDLE CreateJob(bool killOnClose)
{
	HANDLE job = CreateJobObjectW(NULL, NULL);
	if (!job || !killOnClose) return job;
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
	memset(&limits, 0, sizeof(limits));
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
	return job;
}

// The child's ends, each once (the handle list refuses a handle twice; stderr may share stdout's).
static DWORD InheritedHandles(const Launch* launch, HANDLE* inherited)
{
	DWORD count = 0;
	for (int i = 0; i < 3; ++i)
	{
		bool seen = !launch->childEnds[i];
		for (DWORD k = 0; k < count; ++k) seen = seen || inherited[k] == launch->childEnds[i];
		if (!seen) inherited[count++] = launch->childEnds[i];
	}
	return count;
}

static bool CreateAttributes(Launch* launch, HANDLE* inherited, DWORD inheritedCount, HANDLE console)
{
	SIZE_T size = 0;
	InitializeProcThreadAttributeList(NULL, 1, 0, &size);
	launch->attributes = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(size);
	if (!launch->attributes) return false;
	if (!InitializeProcThreadAttributeList(launch->attributes, 1, 0, &size))
	{
		free(launch->attributes);
		launch->attributes = NULL;
		return false;
	}
	if (console) return UpdateProcThreadAttribute(launch->attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console, sizeof(console), NULL, NULL);
	if (inheritedCount == 0) return true;
	return UpdateProcThreadAttribute(launch->attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, inheritedCount * sizeof(HANDLE), NULL, NULL);
}

// A child that shares a stream with this process shares its console too; any other gets a console of its own that
// nobody sees. A terminal's standard handles are left null, so it takes the pseudo console's.
static bool Spawn(const c4_process_options* start, const wchar_t* program, wchar_t* line, const wchar_t* folder, wchar_t* environment,
	c4_process* out, Launch* launch)
{
	HANDLE inherited[3];
	const DWORD inheritedCount = start->terminal ? 0 : InheritedHandles(launch, inherited);
	if (!CreateAttributes(launch, inherited, inheritedCount, out->console)) return false;
	STARTUPINFOEXW info;
	memset(&info, 0, sizeof(info));
	info.StartupInfo.cb = sizeof(info);
	info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	info.StartupInfo.hStdInput = launch->childEnds[kProcessStdin];
	info.StartupInfo.hStdOutput = launch->childEnds[kProcessStdout];
	info.StartupInfo.hStdError = launch->childEnds[kProcessStderr];
	info.lpAttributeList = launch->attributes;
	const bool sharesConsole = start->stdinMode == kProcessInherit || start->stdoutMode == kProcessInherit || start->stderrMode == kProcessInherit;
	DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
	if (!start->terminal && !sharesConsole) flags |= CREATE_NO_WINDOW;
	PROCESS_INFORMATION started;
	if (!CreateProcessW(program, line, NULL, NULL, inheritedCount > 0, flags, environment, folder, &info.StartupInfo, &started)) return false;
	if (!AssignProcessToJobObject(out->job, started.hProcess))
	{
		const DWORD why = GetLastError();
		TerminateProcess(started.hProcess, 1);
		CloseHandle(started.hThread);
		CloseHandle(started.hProcess);
		SetLastError(why);
		return false;
	}
	ResumeThread(started.hThread);
	CloseHandle(started.hThread);
	out->process = started.hProcess;
	out->id = started.dwProcessId;
	return true;
}

// The folder as UTF-16; null for "this process's". `*failed` when it is not UTF-8.
static wchar_t* Folder(const char* folder, bool* failed)
{
	*failed = false;
	if (!folder || !*folder) return NULL;
	wchar_t* wide = c4_utf8_to_wide(folder);
	*failed = wide == NULL;
	return wide;
}

static bool Start(const c4_process_options* start, const wchar_t* program, c4_process* out)
{
	wchar_t* line = c4_process_win32_command_line(start, program);
	bool badFolder = false;
	bool badEnvironment = false;
	wchar_t* folder = Folder(start->workingDirectory, &badFolder);
	wchar_t* environment = c4_process_win32_environment(start, &badEnvironment);
	Launch launch;
	memset(&launch, 0, sizeof(launch));
	bool ok = line && !badFolder && !badEnvironment;
	if (!ok) SetLastError(ERROR_INVALID_PARAMETER);
	if (ok) ok = (out->job = CreateJob(start->killOnClose != 0)) != NULL;
	if (ok) ok = start->terminal ? PrepareConsole(start, out, &launch) : PrepareStreams(start, out, &launch);
	if (ok) ok = Spawn(start, program, line, folder, environment, out, &launch);
	const DWORD why = GetLastError();
	EndLaunch(&launch);
	free(line);
	free(folder);
	free(environment);
	SetLastError(why);
	return ok;
}

int32_t c4_process_start(const c4_process_options* start, c4_process* out, char* error, int64_t errorCapacity)
{
	memset(out, 0, sizeof(*out));
	c4_copy_text("", error, errorCapacity);
	if (!start || !start->program || !*start->program)
	{
		c4_copy_text("no program to start", error, errorCapacity);
		return -1;
	}
	wchar_t* program = c4_process_win32_program(start->program, error, errorCapacity);
	if (!program) return -1;
	const bool started = Start(start, program, out);
	const DWORD why = GetLastError();
	free(program);
	if (started) return 0;
	c4_process_close(out);
	c4_system_error_text(why, error, errorCapacity);
	return -1;
}

// --- the child's life ------------------------------------------------------------------------------------------------

int64_t c4_process_wait(c4_process* process, int32_t timeoutMs)
{
	if (!process || !process->process) return kProcessError;
	const DWORD waited = WaitForSingleObject(process->process, timeoutMs < 0 ? INFINITE : (DWORD)timeoutMs);
	if (waited == WAIT_TIMEOUT) return kProcessTimedOut;
	DWORD code = 0;
	if (waited != WAIT_OBJECT_0 || !GetExitCodeProcess(process->process, &code)) return kProcessError;
	return (int64_t)code;
}

int32_t c4_process_kill(c4_process* process)
{
	if (!process || !process->process) return -1;
	if (process->job && TerminateJobObject(process->job, 1)) return 0;
	return TerminateProcess(process->process, 1) ? 0 : -1;
}

int32_t c4_process_resize(c4_process* process, int32_t columns, int32_t rows)
{
	ResizePseudoConsoleFn resize = (ResizePseudoConsoleFn)(void*)Kernel32("ResizePseudoConsole");
	if (!process || !process->console || !resize || columns <= 0 || rows <= 0 || columns > SHRT_MAX || rows > SHRT_MAX) return -1;
	const COORD size = { (SHORT)columns, (SHORT)rows };
	return SUCCEEDED(resize((HANDLE)process->console, size)) ? 0 : -1;
}

// A pseudo console's output is closed before the console: closing it waits until its last output has gone.
void c4_process_close(c4_process* process)
{
	if (!process) return;
	for (int i = 0; i < 3; ++i) c4_process_win32_close((HANDLE*)&process->pipes[i]);
	ClosePseudoConsoleFn close = (ClosePseudoConsoleFn)(void*)Kernel32("ClosePseudoConsole");
	if (process->console && close) close((HANDLE)process->console);
	process->console = NULL;
	c4_process_win32_close((HANDLE*)&process->job);
	c4_process_win32_close((HANDLE*)&process->process);
	process->id = 0;
}
