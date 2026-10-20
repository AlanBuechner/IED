// The pipes between a parent and its child (ProcessWin32.h, ProcessRuntimeC.h). The parent's end of each is an
// overlapped named pipe, so a read or write can give up after a timeout; the child's end is an ordinary handle. To
// wait for whichever of stdout and stderr has something, a read of no bytes is started on each (it completes when the
// pipe has bytes or has ended, and takes nothing), then the others are cancelled.

#include "ProcessWin32.h"
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "bcrypt.lib")

enum { kPipeBufferBytes = 64 * 1024, kPipeNameUnits = 96, kLargestTransfer = 0x40000000 };

void c4_process_win32_close(HANDLE* handle)
{
	if (*handle && *handle != INVALID_HANDLE_VALUE) CloseHandle(*handle);
	*handle = NULL;
}

// Named after this process and random bits, so no two are alike.
bool c4_process_win32_pipe(bool parentReads, HANDLE* parent, HANDLE* child)
{
	uint64_t nonce = 0;
	BCryptGenRandom(NULL, (PUCHAR)&nonce, sizeof(nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	wchar_t name[kPipeNameUnits];
	swprintf(name, kPipeNameUnits, L"\\\\.\\pipe\\c4-process-%lu-%016llx", GetCurrentProcessId(), (unsigned long long)nonce);
	const DWORD direction = parentReads ? PIPE_ACCESS_INBOUND : PIPE_ACCESS_OUTBOUND;
	*parent = CreateNamedPipeW(name, direction | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, kPipeBufferBytes, kPipeBufferBytes, 0, NULL);
	*child = NULL;
	if (*parent == INVALID_HANDLE_VALUE)
	{
		*parent = NULL;
		return false;
	}
	SECURITY_ATTRIBUTES inheritable = { sizeof(inheritable), NULL, TRUE };
	*child = CreateFileW(name, parentReads ? GENERIC_WRITE : GENERIC_READ, 0, &inheritable, OPEN_EXISTING, 0, NULL);
	if (*child != INVALID_HANDLE_VALUE) return true;
	*child = NULL;
	c4_process_win32_close(parent);
	return false;
}

// The result of an overlapped call that did not finish at once, waiting up to `timeoutMs` and then cancelling it. A
// write cancelled part way counts the bytes that went.
static int64_t Finish(HANDLE pipe, OVERLAPPED* overlapped, int32_t timeoutMs)
{
	if (WaitForSingleObject(overlapped->hEvent, timeoutMs < 0 ? INFINITE : (DWORD)timeoutMs) == WAIT_TIMEOUT) CancelIoEx(pipe, overlapped);
	DWORD moved = 0;
	if (GetOverlappedResult(pipe, overlapped, &moved, TRUE) || moved > 0) return moved;
	const DWORD why = GetLastError();
	return why == ERROR_OPERATION_ABORTED ? kProcessTimedOut : why == ERROR_BROKEN_PIPE ? 0 : kProcessError;
}

// One read or write, given up after `timeoutMs`: the bytes moved, 0 at the end of a pipe the other side closed (a
// write there is -1), kProcessTimedOut, or -1.
static int64_t Transfer(HANDLE pipe, bool writing, void* data, int64_t size, int32_t timeoutMs)
{
	if (!pipe) return kProcessError;
	OVERLAPPED overlapped;
	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!overlapped.hEvent) return kProcessError;
	const DWORD chunk = size > kLargestTransfer ? kLargestTransfer : (DWORD)size;
	DWORD moved = 0;
	const BOOL done = writing ? WriteFile(pipe, data, chunk, &moved, &overlapped) : ReadFile(pipe, data, chunk, &moved, &overlapped);
	const DWORD why = done ? 0 : GetLastError();
	int64_t result = moved;
	if (why == ERROR_IO_PENDING) result = Finish(pipe, &overlapped, timeoutMs);
	else if (why == ERROR_BROKEN_PIPE && !writing) result = 0;
	else if (why != 0) result = kProcessError;
	if (writing && result == 0 && size > 0) result = kProcessError;
	CloseHandle(overlapped.hEvent);
	return result;
}

int64_t c4_process_write(c4_process* process, const void* data, int64_t size, int32_t timeoutMs)
{
	if (!process || size < 0 || (size > 0 && !data)) return kProcessError;
	if (size == 0) return process->pipes[kProcessStdin] ? 0 : kProcessError;
	return Transfer(process->pipes[kProcessStdin], true, (void*)data, size, timeoutMs);
}

int64_t c4_process_read(c4_process* process, int32_t stream, void* buffer, int64_t capacity, int32_t timeoutMs)
{
	if (!process || (stream != kProcessStdout && stream != kProcessStderr) || capacity <= 0 || !buffer) return kProcessError;
	return Transfer(process->pipes[stream], false, buffer, capacity, timeoutMs);
}

// Which of the open `pipes` can be read without waiting (it has bytes, or has ended): its index, kProcessTimedOut, or
// -1.
static int32_t WaitReadable(HANDLE* pipes, int32_t count, int32_t timeoutMs)
{
	OVERLAPPED reads[2];
	HANDLE events[2];
	bool pending[2] = { false, false };
	char none = 0;
	int32_t ready = kProcessError;
	int32_t started = 0;
	while (started < count && ready == kProcessError)
	{
		memset(&reads[started], 0, sizeof(OVERLAPPED));
		events[started] = reads[started].hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		if (!events[started]) break;
		pending[started] = !ReadFile(pipes[started], &none, 0, NULL, &reads[started]) && GetLastError() == ERROR_IO_PENDING;
		if (!pending[started]) ready = started;
		++started;
	}
	if (ready == kProcessError && started == count)
	{
		const DWORD waited = WaitForMultipleObjects((DWORD)count, events, FALSE, timeoutMs < 0 ? INFINITE : (DWORD)timeoutMs);
		if (waited < WAIT_OBJECT_0 + (DWORD)count) ready = (int32_t)(waited - WAIT_OBJECT_0);
		else if (waited == WAIT_TIMEOUT) ready = kProcessTimedOut;
	}
	// A read that finished at once (done, or failed at the pipe's end) signals nothing to wait for.
	for (int32_t i = 0; i < started; ++i)
	{
		DWORD moved = 0;
		if (pending[i])
		{
			CancelIoEx(pipes[i], &reads[i]);
			GetOverlappedResult(pipes[i], &reads[i], &moved, TRUE);
		}
		CloseHandle(events[i]);
	}
	return ready;
}

int64_t c4_process_read_any(c4_process* process, void* buffer, int64_t capacity, int32_t timeoutMs, int32_t* stream)
{
	if (!process || !buffer || capacity <= 0 || !stream) return kProcessError;
	*stream = -1;
	HANDLE pipes[2];
	int32_t streams[2];
	int32_t count = 0;
	for (int32_t s = kProcessStdout; s <= kProcessStderr; ++s)
		if (process->pipes[s])
		{
			pipes[count] = process->pipes[s];
			streams[count++] = s;
		}
	if (count == 0) return 0;
	const int32_t ready = WaitReadable(pipes, count, timeoutMs);
	if (ready < 0) return ready;
	*stream = streams[ready];
	const int64_t read = Transfer(pipes[ready], false, buffer, capacity, kProcessWaitForever);
	if (read == 0) c4_process_win32_close((HANDLE*)&process->pipes[*stream]);
	return read;
}

void c4_process_close_pipe(c4_process* process, int32_t stream)
{
	if (process && stream >= 0 && stream < 3) c4_process_win32_close((HANDLE*)&process->pipes[stream]);
}
