// Local connections over named pipes (LocalRuntimeC.h). A listener keeps a backlog of instances, each with a connect
// in flight, so several clients can connect before one is accepted; accepting hands a connected instance over and puts
// a fresh one in its place. Every call that waits is overlapped, so it can give up after its timeout.

#include "../LocalRuntimeC.h"
#include "../NetRuntimeC.h"
#include "../SystemErrorC.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "advapi32.lib")

enum { kLocalBufferBytes = 64 * 1024, kLargestTransfer = 0x40000000, kNameUnits = 256, kDescriptorUnits = 256, kBacklog = 4 };

// An instance waiting for a client: its connect is in flight, and its event is set once a client is there.
typedef struct Waiting
{
	HANDLE instance;
	OVERLAPPED overlapped;
	bool connected;   // the client came before the connect started: nothing is in flight
} Waiting;

struct c4_local_pipe
{
	HANDLE handle;               // a connection's pipe
	wchar_t* name;               // a listener's pipe name; NULL for a connection
	Waiting waiting[kBacklog];   // a listener's backlog
};

static void NoError(char* error, int64_t capacity) { c4_copy_text("", error, capacity); }

static c4_local_pipe* Failed(DWORD code, char* error, int64_t capacity)
{
	c4_system_error_text(code, error, capacity);
	return NULL;
}

static c4_local_pipe* Refused(const char* why, char* error, int64_t capacity)
{
	c4_copy_text(why, error, capacity);
	return NULL;
}

// "\\.\pipe\<name>"; false for a name that is empty, has a backslash or does not fit.
static bool PipeName(const char* name, wchar_t* out)
{
	if (!name || !name[0] || strchr(name, '\\')) return false;
	wcscpy(out, L"\\\\.\\pipe\\");
	const size_t prefix = wcslen(out);
	const int units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, out + prefix, (int)(kNameUnits - prefix));
	return units > 0;
}

// --- Who may connect ----------------------------------------------------------------------------------------------

// The user a token belongs to (malloc'd), or NULL.
static TOKEN_USER* UserOfToken(HANDLE token)
{
	DWORD size = 0;
	GetTokenInformation(token, TokenUser, NULL, 0, &size);
	TOKEN_USER* user = size ? (TOKEN_USER*)malloc(size) : NULL;
	if (user && GetTokenInformation(token, TokenUser, user, size, &size)) return user;
	free(user);
	return NULL;
}

static TOKEN_USER* UserOf(HANDLE process)
{
	HANDLE token = NULL;
	if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return NULL;
	TOKEN_USER* user = UserOfToken(token);
	CloseHandle(token);
	return user;
}

// A descriptor whose only access entry is the current user's: no other account opens the pipe, whatever its groups.
// LocalFree it.
static PSECURITY_DESCRIPTOR UserOnly(void)
{
	TOKEN_USER* user = UserOf(GetCurrentProcess());
	wchar_t* sid = NULL;
	const BOOL converted = user && ConvertSidToStringSidW(user->User.Sid, &sid);
	free(user);
	if (!converted) return NULL;
	wchar_t text[kDescriptorUnits];
	swprintf(text, kDescriptorUnits, L"D:P(A;;GA;;;%ls)", sid);
	LocalFree(sid);
	PSECURITY_DESCRIPTOR descriptor = NULL;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(text, SDDL_REVISION_1, &descriptor, NULL)) return NULL;
	return descriptor;
}

static bool SameUser(TOKEN_USER* a, TOKEN_USER* b) { return a && b && EqualSid(a->User.Sid, b->User.Sid); }

// Whether the process that listens on `pipe` runs as the current user.
static bool ListenerIsOurs(HANDLE pipe)
{
	ULONG id = 0;
	if (!GetNamedPipeServerProcessId(pipe, &id)) return false;
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
	if (!process) return false;
	TOKEN_USER* theirs = UserOf(process);
	TOKEN_USER* ours = UserOf(GetCurrentProcess());
	const bool same = SameUser(theirs, ours);
	free(theirs);
	free(ours);
	CloseHandle(process);
	return same;
}

// --- Listening ----------------------------------------------------------------------------------------------------

static HANDLE NewInstance(const wchar_t* name, bool first)
{
	PSECURITY_DESCRIPTOR descriptor = UserOnly();
	if (!descriptor) return INVALID_HANDLE_VALUE;
	SECURITY_ATTRIBUTES attributes = { sizeof(attributes), descriptor, FALSE };
	const DWORD open = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
	const DWORD mode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
	HANDLE pipe = CreateNamedPipeW(name, open, mode, PIPE_UNLIMITED_INSTANCES, kLocalBufferBytes, kLocalBufferBytes, 0, &attributes);
	const DWORD why = GetLastError();
	LocalFree(descriptor);
	SetLastError(why);
	return pipe;
}

// Starts a connect on the slot's instance: 0, or the system's error.
static DWORD StartConnect(Waiting* slot)
{
	HANDLE event = slot->overlapped.hEvent;
	memset(&slot->overlapped, 0, sizeof(OVERLAPPED));
	slot->overlapped.hEvent = event;
	ResetEvent(event);
	slot->connected = false;
	const DWORD why = ConnectNamedPipe(slot->instance, &slot->overlapped) ? 0 : GetLastError();
	if (why == ERROR_IO_PENDING) return 0;
	if (why == 0 || why == ERROR_PIPE_CONNECTED)
	{
		slot->connected = true;
		SetEvent(event);
		return 0;
	}
	return why;
}

// A fresh instance in the slot, waiting for a client (`first` claims the name): 0, or the system's error.
static DWORD StartWaiting(c4_local_pipe* listener, int slot, bool first)
{
	Waiting* waiting = &listener->waiting[slot];
	waiting->instance = NewInstance(listener->name, first);
	if (waiting->instance == INVALID_HANDLE_VALUE)
	{
		waiting->instance = NULL;
		return GetLastError();
	}
	const DWORD why = StartConnect(waiting);
	if (why == 0) return 0;
	CloseHandle(waiting->instance);
	waiting->instance = NULL;
	return why;
}

static void StopWaiting(Waiting* waiting)
{
	if (waiting->instance)
	{
		DWORD unused = 0;
		if (!waiting->connected && CancelIoEx(waiting->instance, &waiting->overlapped))
			GetOverlappedResult(waiting->instance, &waiting->overlapped, &unused, TRUE);
		CloseHandle(waiting->instance);
	}
	if (waiting->overlapped.hEvent) CloseHandle(waiting->overlapped.hEvent);
	memset(waiting, 0, sizeof(Waiting));
}

static void CloseListener(c4_local_pipe* listener)
{
	for (int i = 0; i < kBacklog; ++i) StopWaiting(&listener->waiting[i]);
	free(listener->name);
	free(listener);
}

c4_local_pipe* c4_local_listen(const char* name, char* error, int64_t errorCapacity)
{
	NoError(error, errorCapacity);
	c4_local_pipe* listener = (c4_local_pipe*)calloc(1, sizeof(c4_local_pipe));
	wchar_t* full = (wchar_t*)malloc(kNameUnits * sizeof(wchar_t));
	if (!listener || !full || !PipeName(name, full))
	{
		free(listener);
		free(full);
		return Refused("not a local name: one that is not empty, has no backslash and fits 200 characters", error, errorCapacity);
	}
	listener->name = full;
	for (int i = 0; i < kBacklog; ++i) listener->waiting[i].overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	for (int i = 0; i < kBacklog; ++i)
	{
		const DWORD why = listener->waiting[i].overlapped.hEvent ? StartWaiting(listener, i, i == 0) : GetLastError();
		if (why == 0) continue;
		CloseListener(listener);
		if (i == 0 && (why == ERROR_ACCESS_DENIED || why == ERROR_PIPE_BUSY)) return Refused("the name is taken: something listens there already", error, errorCapacity);
		return Failed(why, error, errorCapacity);
	}
	return listener;
}

static c4_local_pipe* Wrap(HANDLE handle)
{
	c4_local_pipe* pipe = (c4_local_pipe*)calloc(1, sizeof(c4_local_pipe));
	if (!pipe)
	{
		CloseHandle(handle);
		return NULL;
	}
	pipe->handle = handle;
	return pipe;
}

// The slot whose client is there, within `timeoutMs`: its index, -1 when none came, or -2 when waiting failed.
static int WaitForClient(c4_local_pipe* listener, int32_t timeoutMs)
{
	HANDLE events[kBacklog];
	int slots[kBacklog];
	DWORD count = 0;
	for (int i = 0; i < kBacklog; ++i)
	{
		if (!listener->waiting[i].instance) continue;
		events[count] = listener->waiting[i].overlapped.hEvent;
		slots[count++] = i;
	}
	if (count == 0) return -2;
	const DWORD which = WaitForMultipleObjects(count, events, FALSE, timeoutMs < 0 ? INFINITE : (DWORD)timeoutMs);
	if (which == WAIT_TIMEOUT) return -1;
	return which < WAIT_OBJECT_0 + count ? slots[which - WAIT_OBJECT_0] : -2;
}

c4_local_pipe* c4_local_accept(c4_local_pipe* listener, int32_t timeoutMs, int32_t* timedOut, char* error, int64_t errorCapacity)
{
	*timedOut = 0;
	NoError(error, errorCapacity);
	if (!listener || !listener->name) return Refused("not a listener", error, errorCapacity);
	const int slot = WaitForClient(listener, timeoutMs);
	if (slot == -2) return Failed(GetLastError(), error, errorCapacity);
	if (slot == -1)
	{
		*timedOut = 1;
		return Refused("no client came in time", error, errorCapacity);
	}
	Waiting* waiting = &listener->waiting[slot];
	DWORD unused = 0;
	const bool came = waiting->connected || GetOverlappedResult(waiting->instance, &waiting->overlapped, &unused, FALSE);
	if (!came)
	{
		// The client left before it was accepted: the instance waits again.
		DisconnectNamedPipe(waiting->instance);
		const DWORD why = StartConnect(waiting);
		*timedOut = why == 0;
		return why == 0 ? Refused("no client came in time", error, errorCapacity) : Failed(why, error, errorCapacity);
	}
	HANDLE connected = waiting->instance;
	waiting->instance = NULL;
	StartWaiting(listener, slot, false);
	return Wrap(connected);
}

// --- Connecting and talking ---------------------------------------------------------------------------------------

c4_local_pipe* c4_local_connect(const char* name, int32_t timeoutMs, int32_t* timedOut, char* error, int64_t errorCapacity)
{
	*timedOut = 0;
	NoError(error, errorCapacity);
	wchar_t full[kNameUnits];
	if (!PipeName(name, full)) return Refused("not a local name", error, errorCapacity);
	const ULONGLONG deadline = GetTickCount64() + (ULONGLONG)(timeoutMs < 0 ? 0 : timeoutMs);
	for (;;)
	{
		HANDLE pipe = CreateFileW(full, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
		if (pipe != INVALID_HANDLE_VALUE)
		{
			if (ListenerIsOurs(pipe)) return Wrap(pipe);
			CloseHandle(pipe);
			return Refused("the listener on that name runs as another user", error, errorCapacity);
		}
		const DWORD why = GetLastError();
		if (why == ERROR_FILE_NOT_FOUND) return Refused("no one listens on that name", error, errorCapacity);
		if (why != ERROR_PIPE_BUSY) return Failed(why, error, errorCapacity);
		const ULONGLONG now = GetTickCount64();
		if (timeoutMs >= 0 && now >= deadline)
		{
			*timedOut = 1;
			return Refused("the listener stayed busy", error, errorCapacity);
		}
		WaitNamedPipeW(full, timeoutMs < 0 ? NMPWAIT_WAIT_FOREVER : (DWORD)(deadline - now));
	}
}

// One read or write, given up after `timeoutMs`.
static int64_t Transfer(c4_local_pipe* pipe, bool writing, void* data, int64_t size, int32_t timeoutMs)
{
	if (!pipe || !pipe->handle || pipe->name || size < 0 || (size > 0 && !data)) return kNetError;
	if (size == 0) return 0;
	OVERLAPPED overlapped;
	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!overlapped.hEvent) return kNetError;
	const DWORD chunk = size > kLargestTransfer ? kLargestTransfer : (DWORD)size;
	DWORD moved = 0;
	const BOOL done = writing ? WriteFile(pipe->handle, data, chunk, &moved, &overlapped) : ReadFile(pipe->handle, data, chunk, &moved, &overlapped);
	DWORD why = done ? 0 : GetLastError();
	if (why == ERROR_IO_PENDING)
	{
		if (WaitForSingleObject(overlapped.hEvent, timeoutMs < 0 ? INFINITE : (DWORD)timeoutMs) == WAIT_TIMEOUT) CancelIoEx(pipe->handle, &overlapped);
		why = GetOverlappedResult(pipe->handle, &overlapped, &moved, TRUE) ? 0 : GetLastError();
	}
	CloseHandle(overlapped.hEvent);
	if (why == ERROR_OPERATION_ABORTED) return moved > 0 ? (int64_t)moved : kNetTimedOut;
	if (why == ERROR_BROKEN_PIPE || why == ERROR_PIPE_NOT_CONNECTED) return writing ? kNetError : 0;
	if (why != 0) return kNetError;
	return moved;
}

int64_t c4_local_send(c4_local_pipe* pipe, const void* data, int64_t size, int32_t timeoutMs)
{
	return Transfer(pipe, true, (void*)data, size, timeoutMs);
}

int64_t c4_local_receive(c4_local_pipe* pipe, void* buffer, int64_t capacity, int32_t timeoutMs)
{
	return Transfer(pipe, false, buffer, capacity, timeoutMs);
}

void c4_local_close(c4_local_pipe* pipe)
{
	if (!pipe) return;
	if (pipe->name)
	{
		CloseListener(pipe);
		return;
	}
	if (pipe->handle) CloseHandle(pipe->handle);
	free(pipe);
}

int64_t c4_local_user_id(char* out, int64_t capacity)
{
	TOKEN_USER* user = UserOf(GetCurrentProcess());
	char* sid = NULL;
	const BOOL converted = user && ConvertSidToStringSidA(user->User.Sid, &sid);
	free(user);
	if (!converted) return -1;
	const int64_t length = (int64_t)strlen(sid);
	c4_copy_text(sid, out, capacity);
	LocalFree(sid);
	return length;
}
