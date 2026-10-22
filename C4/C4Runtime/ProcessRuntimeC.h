#pragma once
// Child processes behind Bedrock's Process namespace (D-68): started with pipes or under a pseudo console, each in a
// job object so that killing it ends the processes it started too, read and written and waited for with timeouts.
// One implementation per OS (Process/*Win32.c). Text is UTF-8. A call that can fail writes the system's message and
// code into `error` (cut at a whole character to `errorCapacity`), empty on success.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Where a child's standard stream goes: the parent's own, a pipe the parent reads or writes, nowhere (NUL), or - for
// stderr - the same pipe as stdout.
enum { kProcessInherit = 0, kProcessPipe = 1, kProcessNull = 2, kProcessToOutput = 3 };

enum { kProcessStdin = 0, kProcessStdout = 1, kProcessStderr = 2 };

// Besides a count or an exit code, a call returns one of these.
enum { kProcessError = -1, kProcessTimedOut = -2 };

// How long a call waits: milliseconds, 0 to only check, or kProcessWaitForever.
enum { kProcessWaitForever = -1 };

// Bedrock's Process::StartInfo, flattened.
typedef struct c4_process_options
{
	const char* program;              // a path, or a name looked for as the system does (".exe" added when it has no extension)
	const char* const* arguments;     // not counting the program; quoted as CommandLineToArgvW reads them back
	int32_t argumentCount;
	const char* workingDirectory;     // null or empty: this process's
	const char* const* environment;   // "NAME=value" sets a variable, "NAME" removes one (names compared as the OS does)
	int32_t environmentCount;
	int32_t inheritEnvironment;       // non-zero: `environment` changes this process's; zero: it is the whole environment
	int32_t stdinMode;                // kProcess* above
	int32_t stdoutMode;
	int32_t stderrMode;
	int32_t terminal;                 // non-zero: under a pseudo console of `columns` x `rows`; the modes do not apply
	int32_t columns;
	int32_t rows;
	int32_t killOnClose;              // non-zero: closing the handle ends the child and what it started
	const char* shellCommand;         // non-empty: instead of a program and arguments, a command line the system's shell
	                                  // runs as written (cmd.exe /d /s /c "...")
} c4_process_options;

// A started child: its handles, null when it has none of that kind.
typedef struct c4_process
{
	void* process;
	void* job;
	void* pipes[3];    // the parent's ends: stdin (written), stdout and stderr (read)
	void* console;     // the pseudo console
	uint32_t id;
} c4_process;

// Starts a child. 0, or -1 with `error` (no such program, a folder that does not exist, ...).
int32_t c4_process_start(const c4_process_options* start, c4_process* out, char* error, int64_t errorCapacity);
// Bytes written to the child's input (a terminal's keyboard), kProcessTimedOut, or -1 (the input is closed or the
// child is gone).
int64_t c4_process_write(c4_process* process, const void* data, int64_t size, int32_t timeoutMs);
// Bytes read from the child's stdout or stderr (a terminal's screen is stdout): 0 once the child and everything it
// started have closed it, kProcessTimedOut, or -1.
int64_t c4_process_read(c4_process* process, int32_t stream, void* buffer, int64_t capacity, int32_t timeoutMs);
// Reads whichever of stdout and stderr has bytes first, setting `*stream` to it. 0 when one has ended (its pipe is
// then closed, so the next call waits on the other), with `*stream` -1 once neither is open.
int64_t c4_process_read_any(c4_process* process, void* buffer, int64_t capacity, int32_t timeoutMs, int32_t* stream);
// Closes the parent's end of a pipe: closing stdin is the end of the child's input.
void c4_process_close_pipe(c4_process* process, int32_t stream);
// The child's exit code once it has ended (0 to 4294967295), kProcessTimedOut, or -1.
int64_t c4_process_wait(c4_process* process, int32_t timeoutMs);
// Ends the child and every process it started. 0 or -1.
int32_t c4_process_kill(c4_process* process);
// A terminal's new size. 0 or -1 (not a terminal).
int32_t c4_process_resize(c4_process* process, int32_t columns, int32_t rows);
// Releases the handles; with killOnClose, whatever still runs in the child's job ends.
void c4_process_close(c4_process* process);

// What a running process uses (Bedrock's Process::Usage, Process/UsageWin32.c).
typedef struct c4_process_counters
{
	int64_t cpuTimeNs;          // processor time in user and kernel mode so far, every thread's
	int64_t workingSetBytes;    // its memory in RAM now
	int64_t privateBytes;       // memory committed to it alone
	int64_t peakPrivateBytes;
} c4_process_counters;

// What process `id` uses: 0, or -1 when there is no such process or the system will not say (`usage` zeroed).
int32_t c4_process_usage(uint32_t id, c4_process_counters* usage);

#ifdef __cplusplus
}
#endif
