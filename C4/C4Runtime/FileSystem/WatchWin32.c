// Watching a folder for Bedrock's FS::Watcher (FileSystemRuntimeC.h) with ReadDirectoryChangesW. A request is kept
// pending from c4_fs_watch_open on: when one completes, the next starts at once into the other of two buffers while
// the first is handed out record by record, and between requests the system keeps what changes. A rename arrives as
// two records, the old name then the new, which become one change.

#include "FileSystemWin32.h"
#include "PathRuntimeC.h"
#include "SystemErrorC.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum { kWatchBufferBytes = 64 * 1024 };

static const DWORD kWatchFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
	FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;

// A growable UTF-8 string a change's path is handed out in.
typedef struct Text { char* bytes; int64_t capacity; } Text;

typedef struct Watch
{
	HANDLE folder;
	OVERLAPPED overlapped;
	BOOL recursive;
	bool pending;                      // a request is outstanding into buffers[requested]
	int requested;
	DWORD* buffers[2];                 // DWORD-aligned, as the records must be
	const BYTE* next;                  // the next record of the buffer being handed out; null when it is used up
	bool overflowed;                   // the system dropped changes: report it before anything else
	Text path;
	Text oldPath;
	bool hasOld;                       // oldPath holds a rename's old name, waiting for the new one
} Watch;

static bool Store(Text* text, const wchar_t* name, DWORD units)
{
	const int needed = WideCharToMultiByte(CP_UTF8, 0, name, (int)units, NULL, 0, NULL, NULL);
	if (needed + 1 > text->capacity)
	{
		char* grown = (char*)realloc(text->bytes, (size_t)needed + 1);
		if (!grown) return false;
		text->bytes = grown;
		text->capacity = needed + 1;
	}
	WideCharToMultiByte(CP_UTF8, 0, name, (int)units, text->bytes, needed, NULL, NULL);
	text->bytes[needed] = 0;
	return true;
}

static const char* TextOf(const Text* text) { return text->bytes ? text->bytes : ""; }

static bool Request(Watch* watch)
{
	ResetEvent(watch->overlapped.hEvent);
	watch->pending = ReadDirectoryChangesW(watch->folder, watch->buffers[watch->requested], kWatchBufferBytes, watch->recursive, kWatchFilter,
		NULL, &watch->overlapped, NULL) != 0;
	return watch->pending;
}

static void CloseWatch(Watch* watch)
{
	if (watch->pending)
	{
		DWORD moved = 0;
		CancelIoEx(watch->folder, &watch->overlapped);
		GetOverlappedResult(watch->folder, &watch->overlapped, &moved, TRUE);
	}
	if (watch->folder != INVALID_HANDLE_VALUE) CloseHandle(watch->folder);
	if (watch->overlapped.hEvent) CloseHandle(watch->overlapped.hEvent);
	free(watch->buffers[0]);
	free(watch->buffers[1]);
	free(watch->path.bytes);
	free(watch->oldPath.bytes);
	free(watch);
}

void* c4_fs_watch_open(const char* path, int32_t recursive, char* error, int64_t errorCapacity)
{
	c4_copy_text("", error, errorCapacity);
	Watch* watch = (Watch*)calloc(1, sizeof(Watch));
	wchar_t* wide = c4_utf8_to_wide(path);
	if (!watch || !wide)
	{
		free(watch);
		free(wide);
		c4_system_error_text(watch ? ERROR_INVALID_NAME : ERROR_NOT_ENOUGH_MEMORY, error, errorCapacity);
		return NULL;
	}
	watch->folder = CreateFileW(wide, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
	DWORD why = watch->folder == INVALID_HANDLE_VALUE ? GetLastError() : 0;
	free(wide);
	watch->recursive = recursive != 0;
	watch->overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	watch->buffers[0] = (DWORD*)malloc(kWatchBufferBytes);
	watch->buffers[1] = (DWORD*)malloc(kWatchBufferBytes);
	if (!why && (!watch->overlapped.hEvent || !watch->buffers[0] || !watch->buffers[1])) why = ERROR_NOT_ENOUGH_MEMORY;
	if (!why && !Request(watch)) why = GetLastError();
	if (!why) return watch;
	c4_system_error_text(why, error, errorCapacity);
	CloseWatch(watch);
	return NULL;
}

// --- handing out records ---------------------------------------------------------------------------------------------

// The change a record makes, into `out`; false for the old name of a rename, which waits for its new name.
static bool Translate(Watch* watch, const FILE_NOTIFY_INFORMATION* record, c4_fs_change* out)
{
	Text* into = record->Action == FILE_ACTION_RENAMED_OLD_NAME ? &watch->oldPath : &watch->path;
	Store(into, record->FileName, record->FileNameLength / sizeof(wchar_t));
	if (record->Action == FILE_ACTION_RENAMED_OLD_NAME)
	{
		watch->hasOld = true;
		return false;
	}
	out->path = TextOf(&watch->path);
	out->oldPath = "";
	switch (record->Action)
	{
	case FILE_ACTION_ADDED: out->kind = kFsAdded; break;
	case FILE_ACTION_REMOVED: out->kind = kFsRemoved; break;
	case FILE_ACTION_RENAMED_NEW_NAME: out->kind = watch->hasOld ? kFsRenamed : kFsAdded; break;
	default: out->kind = kFsModified; break;
	}
	if (out->kind == kFsRenamed) out->oldPath = TextOf(&watch->oldPath);
	watch->hasOld = false;
	return true;
}

// A rename's old name with no new name after it: the entry left the watched folder.
static bool LoneOldName(Watch* watch, c4_fs_change* out)
{
	watch->hasOld = false;
	out->kind = kFsRemoved;
	out->path = TextOf(&watch->oldPath);
	out->oldPath = "";
	return true;
}

// The next change already received; false when there is none.
static bool Take(Watch* watch, c4_fs_change* out)
{
	if (watch->overflowed)
	{
		watch->overflowed = false;
		out->kind = kFsOverflow;
		out->path = "";
		out->oldPath = "";
		return true;
	}
	while (watch->next)
	{
		const FILE_NOTIFY_INFORMATION* record = (const FILE_NOTIFY_INFORMATION*)watch->next;
		if (watch->hasOld && record->Action != FILE_ACTION_RENAMED_NEW_NAME) return LoneOldName(watch, out);
		watch->next = record->NextEntryOffset ? watch->next + record->NextEntryOffset : NULL;
		if (Translate(watch, record, out)) return true;
	}
	return false;
}

// Waits for the pending request: 1 when it completed (its records ready to hand out, the next request started),
// kFsTimedOut, or -1.
static int32_t Receive(Watch* watch, int32_t timeoutMs)
{
	if (WaitForSingleObject(watch->overlapped.hEvent, timeoutMs < 0 ? INFINITE : (DWORD)timeoutMs) == WAIT_TIMEOUT) return kFsTimedOut;
	DWORD bytes = 0;
	const BOOL done = GetOverlappedResult(watch->folder, &watch->overlapped, &bytes, FALSE);
	watch->pending = false;
	if (!done && GetLastError() != ERROR_NOTIFY_ENUM_DIR) return -1;
	const int completed = watch->requested;
	watch->overflowed = !done || bytes == 0;
	watch->hasOld = watch->hasOld && !watch->overflowed;
	watch->next = watch->overflowed ? NULL : (const BYTE*)watch->buffers[completed];
	watch->requested = 1 - completed;
	Request(watch);   // when it fails, the next call reports why once these records are handed out
	return 1;
}

int32_t c4_fs_watch_next(void* handle, int32_t timeoutMs, c4_fs_change* out, char* error, int64_t errorCapacity)
{
	c4_copy_text("", error, errorCapacity);
	Watch* watch = (Watch*)handle;
	if (!watch || !out) return -1;
	while (!Take(watch, out))
	{
		if (!watch->pending && !Request(watch))
		{
			c4_system_error_text(GetLastError(), error, errorCapacity);
			return -1;
		}
		const int32_t received = Receive(watch, timeoutMs);
		if (received == kFsTimedOut) return kFsTimedOut;
		if (received < 0)
		{
			c4_system_error_text(GetLastError(), error, errorCapacity);
			return -1;
		}
	}
	return 1;
}

void c4_fs_watch_close(void* handle)
{
	if (handle) CloseWatch((Watch*)handle);
}
