// Directories, rename and the current directory behind the fs_* builtins (Bedrock's FS::Directory and
// friends). Paths are UTF-8 on every platform; on Windows they go to the wide API (PathRuntimeC.h).

#include "PathRuntimeC.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Bedrock's FS::EntryKind. A link (a symbolic link or a junction) is never followed.
enum { kEntryFile = 0, kEntryDirectory = 1, kEntryLink = 2 };

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

wchar_t* c4_utf8_to_wide(const char* text)
{
	if (!text) return NULL;
	int units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
	if (units <= 0) return NULL;
	wchar_t* wide = (wchar_t*)malloc((size_t)units * sizeof(wchar_t));
	if (wide && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, units) != units)
	{
		free(wide);
		return NULL;
	}
	return wide;
}

int64_t c4_wide_to_utf8(const wchar_t* wide, char* out, int64_t cap)
{
	int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
	if (bytes > 0 && out && cap >= bytes) WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, bytes, NULL, NULL);
	return bytes > 0 ? (int64_t)bytes - 1 : -1;
}

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

int32_t c4_fs_is_link(const char* path)
{
	DWORD attributes = Attributes(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ? 1 : 0;
}

static int32_t KindOf(DWORD attributes)
{
	if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return kEntryLink;
	return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? kEntryDirectory : kEntryFile;
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

int32_t c4_fs_rename(const char* from, const char* to)
{
	wchar_t* source = c4_utf8_to_wide(from);
	wchar_t* target = c4_utf8_to_wide(to);
	BOOL moved = source && target && MoveFileExW(source, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
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
		if (kind) *kind = KindOf(dir->data.dwFileAttributes);
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

#else
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int32_t c4_fs_is_directory(const char* path)
{
	struct stat st;
	return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode) ? 1 : 0;
}

int32_t c4_fs_is_link(const char* path)
{
	struct stat st;
	return path && lstat(path, &st) == 0 && S_ISLNK(st.st_mode) ? 1 : 0;
}

int32_t c4_fs_make_directory(const char* path) { return path && mkdir(path, 0777) == 0 ? 0 : -1; }

int32_t c4_fs_remove_directory(const char* path) { return path && rmdir(path) == 0 ? 0 : -1; }

int32_t c4_fs_rename(const char* from, const char* to) { return from && to && rename(from, to) == 0 ? 0 : -1; }

static int64_t TextToOut(const char* text, char* out, int64_t cap)
{
	int64_t length = (int64_t)strlen(text);
	if (out && cap > length) memcpy(out, text, (size_t)length + 1);
	return length;
}

int64_t c4_fs_current_directory(char* out, int64_t cap)
{
	char buffer[4096];
	return getcwd(buffer, sizeof(buffer)) ? TextToOut(buffer, out, cap) : -1;
}

int64_t c4_fs_temp_directory(char* out, int64_t cap)
{
	const char* dir = getenv("TMPDIR");
	return TextToOut(dir && *dir ? dir : "/tmp", out, cap);
}

typedef struct C4Directory
{
	DIR* dir;
	char* path;
	struct dirent* pending;
} C4Directory;

void* c4_fs_directory_open(const char* path)
{
	DIR* opened = path && *path ? opendir(path) : NULL;
	if (!opened) return NULL;
	C4Directory* dir = (C4Directory*)calloc(1, sizeof(C4Directory));
	if (!dir) { closedir(opened); return NULL; }
	dir->dir = opened;
	dir->path = strdup(path);
	return dir;
}

static int32_t EntryKind(const C4Directory* dir, const struct dirent* entry)
{
	char full[4096];
	struct stat st;
	snprintf(full, sizeof(full), "%s/%s", dir->path, entry->d_name);
	if (lstat(full, &st) != 0) return kEntryFile;
	if (S_ISLNK(st.st_mode)) return kEntryLink;
	return S_ISDIR(st.st_mode) ? kEntryDirectory : kEntryFile;
}

int32_t c4_fs_directory_next(void* handle, char* name, int32_t cap, int32_t* kind)
{
	C4Directory* dir = (C4Directory*)handle;
	if (!dir || !name || cap <= 0) return -1;
	for (;;)
	{
		struct dirent* entry = dir->pending ? dir->pending : readdir(dir->dir);
		dir->pending = NULL;
		if (!entry) return 0;
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
		size_t length = strlen(entry->d_name);
		if ((int64_t)length + 1 > cap) { dir->pending = entry; return -1; }
		memcpy(name, entry->d_name, length + 1);
		if (kind) *kind = EntryKind(dir, entry);
		return (int32_t)length;
	}
}

void c4_fs_directory_close(void* handle)
{
	C4Directory* dir = (C4Directory*)handle;
	if (!dir) return;
	closedir(dir->dir);
	free(dir->path);
	free(dir);
}
#endif
