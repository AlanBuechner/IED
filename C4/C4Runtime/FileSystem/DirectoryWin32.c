// Directories, rename and the current directory behind the fs_* builtins (Bedrock's FS::Directory and friends).
// Paths are UTF-8; they go to the wide API (PathRuntimeC.h).

#include "FileSystemWin32.h"
#include "PathRuntimeC.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static DWORD Attributes(const char* path)
{
	wchar_t* wide = c4_utf8_to_wide(path);
	DWORD attributes = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
	free(wide);
	return attributes;
}

int32_t c4_fs_is_directory(const char* path)
{
	DWORD attributes = Attributes(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

// A symbolic link or a junction; any other reparse point is the file or folder it stands for (BUG-276).
int32_t c4_fs_is_link(const char* path)
{
	DWORD attributes = Attributes(path);
	if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
	wchar_t* wide = c4_utf8_to_wide(path);
	const DWORD tag = wide && !wcspbrk(wide, L"*?") ? c4_fs_win32_reparse_tag(wide) : 0;
	free(wide);
	return c4_fs_win32_kind(attributes, tag) == kEntryLink ? 1 : 0;
}

int32_t c4_fs_make_directory(const char* path)
{
	wchar_t* wide = c4_utf8_to_wide(path);
	BOOL made = wide && CreateDirectoryW(wide, NULL);
	free(wide);
	return made ? 0 : -1;
}

int32_t c4_fs_remove_directory(const char* path)
{
	wchar_t* wide = c4_utf8_to_wide(path);
	BOOL removed = wide && RemoveDirectoryW(wide);
	free(wide);
	return removed ? 0 : -1;
}

// How long a move over a file waits out another process that holds the file open, and the longest pause between tries.
enum { kMoveRetryMs = 2000, kMoveLongestPauseMs = 64 };

// Whether a move failed only because another process holds the target open for a moment without FILE_SHARE_DELETE (a
// virus scanner reading a file just written, an editor, a reader): a directory or a read-only file in the way is no
// such moment (BUG-395).
static int c4_target_held(DWORD error, const wchar_t* target)
{
	if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION) return 0;
	DWORD attributes = GetFileAttributesW(target);
	return attributes == INVALID_FILE_ATTRIBUTES || !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY));
}

// MoveFileExW, tried again with a growing pause while the target is held, for up to kMoveRetryMs.
static BOOL c4_move_over(const wchar_t* source, const wchar_t* target, DWORD flags)
{
	DWORD pause = 1;
	for (DWORD waited = 0; ; waited += pause, pause = pause < kMoveLongestPauseMs ? pause * 2 : kMoveLongestPauseMs)
	{
		if (MoveFileExW(source, target, flags)) return TRUE;
		if (waited >= kMoveRetryMs || !c4_target_held(GetLastError(), target)) return FALSE;
		Sleep(pause);
	}
}

int32_t c4_fs_rename(const char* from, const char* to)
{
	wchar_t* source = c4_utf8_to_wide(from);
	wchar_t* target = c4_utf8_to_wide(to);
	BOOL moved = source && target && c4_move_over(source, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
	free(source);
	free(target);
	return moved ? 0 : -1;
}

// A rename over `to` in the same folder that is on the disk when it returns (FS::WriteFileAtomically).
int32_t c4_fs_replace(const char* from, const char* to)
{
	wchar_t* source = c4_utf8_to_wide(from);
	wchar_t* target = c4_utf8_to_wide(to);
	BOOL moved = source && target && c4_move_over(source, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	free(source);
	free(target);
	return moved ? 0 : -1;
}

int64_t c4_fs_current_directory(char* out, int64_t cap)
{
	DWORD units = GetCurrentDirectoryW(0, NULL);
	if (units == 0) return -1;
	wchar_t* wide = (wchar_t*)malloc((size_t)units * sizeof(wchar_t));
	int64_t length = wide && GetCurrentDirectoryW(units, wide) + 1 == units ? c4_wide_to_utf8(wide, out, cap) : -1;
	free(wide);
	return length;
}

// The directory for temporary files (GetTempPath: TMP, TEMP, USERPROFILE or the Windows folder).
int64_t c4_fs_temp_directory(char* out, int64_t cap)
{
	wchar_t wide[MAX_PATH + 2];
	DWORD units = GetTempPathW(MAX_PATH + 2, wide);
	if (units == 0 || units > MAX_PATH + 1) return -1;
	return c4_wide_to_utf8(wide, out, cap);
}

// A listing: FindFirstFile hands over the first entry when it opens, so `pending` says whether `data` holds
// an entry not yet returned.
typedef struct C4Directory
{
	HANDLE find;
	WIN32_FIND_DATAW data;
	int pending;
} C4Directory;

void* c4_fs_directory_open(const char* path)
{
	if (!path || !*path || !c4_fs_is_directory(path)) return NULL;
	size_t length = strlen(path);
	char last = path[length - 1];
	char* pattern = (char*)malloc(length + 3);
	if (!pattern) return NULL;
	memcpy(pattern, path, length);
	strcpy(pattern + length, last == '/' || last == '\\' ? "*" : "/*");
	wchar_t* wide = c4_utf8_to_wide(pattern);
	free(pattern);
	C4Directory* dir = wide ? (C4Directory*)calloc(1, sizeof(C4Directory)) : NULL;
	if (dir)
	{
		dir->find = FindFirstFileExW(wide, FindExInfoBasic, &dir->data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
		dir->pending = dir->find != INVALID_HANDLE_VALUE;
		if (!dir->pending && GetLastError() != ERROR_FILE_NOT_FOUND) { free(dir); dir = NULL; }
	}
	free(wide);
	return dir;
}

static int IsDotEntry(const wchar_t* name)
{
	return name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0));
}

// The next entry (not `.` or `..`): its UTF-8 name into `name` and its kind into `*kind`. Returns the name's
// length, 0 at the end, -1 when `name` is too small (the entry stays for the next call).
int32_t c4_fs_directory_next(void* handle, char* name, int32_t cap, int32_t* kind)
{
	C4Directory* dir = (C4Directory*)handle;
	if (!dir || !name || cap <= 0) return -1;
	for (;;)
	{
		if (!dir->pending)
		{
			if (dir->find == INVALID_HANDLE_VALUE || !FindNextFileW(dir->find, &dir->data)) return 0;
			dir->pending = 1;
		}
		if (IsDotEntry(dir->data.cFileName)) { dir->pending = 0; continue; }
		int bytes = WideCharToMultiByte(CP_UTF8, 0, dir->data.cFileName, -1, NULL, 0, NULL, NULL);
		if (bytes <= 0 || bytes > cap) return -1;
		WideCharToMultiByte(CP_UTF8, 0, dir->data.cFileName, -1, name, cap, NULL, NULL);
		if (kind) *kind = c4_fs_win32_kind(dir->data.dwFileAttributes, dir->data.dwReserved0);
		dir->pending = 0;
		return bytes - 1;
	}
}

void c4_fs_directory_close(void* handle)
{
	C4Directory* dir = (C4Directory*)handle;
	if (!dir) return;
	if (dir->find != INVALID_HANDLE_VALUE) FindClose(dir->find);
	free(dir);
}
