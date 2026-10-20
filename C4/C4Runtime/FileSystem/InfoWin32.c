// What is at a path, copies, and absolute and real paths for Bedrock's FS namespace (FileSystemRuntimeC.h).

#include "FileSystemWin32.h"
#include "PathRuntimeC.h"
#include "SystemErrorC.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// FILETIME counts 100 ns from 1601; Unix time counts from 1970.
enum { kNsPerTick = 100 };
static const int64_t kUnixEpochTicks = 116444736000000000LL;

static int64_t UnixNs(FILETIME time)
{
	const int64_t ticks = (int64_t)(((uint64_t)time.dwHighDateTime << 32) | time.dwLowDateTime);
	return (ticks - kUnixEpochTicks) * kNsPerTick;
}

static FILETIME FileTime(int64_t unixNs)
{
	const uint64_t ticks = (uint64_t)(unixNs / kNsPerTick + kUnixEpochTicks);
	FILETIME time = { (DWORD)ticks, (DWORD)(ticks >> 32) };
	return time;
}

static const DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

// --- information -----------------------------------------------------------------------------------------------------

static void Describe(int32_t kind, DWORD attributes, DWORD sizeHigh, DWORD sizeLow, FILETIME created, FILETIME accessed, FILETIME modified,
	c4_fs_file_info* out)
{
	out->kind = kind;
	out->readOnly = (attributes & FILE_ATTRIBUTE_READONLY) != 0;
	out->size = kind == kEntryFile ? (int64_t)(((uint64_t)sizeHigh << 32) | sizeLow) : 0;
	out->createdNs = UnixNs(created);
	out->accessedNs = UnixNs(accessed);
	out->modifiedNs = UnixNs(modified);
}

// The entry itself: a link is described, not followed.
static bool Itself(const wchar_t* path, c4_fs_file_info* out)
{
	WIN32_FILE_ATTRIBUTE_DATA data;
	if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) return false;
	const DWORD tag = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? c4_fs_win32_reparse_tag(path) : 0;
	Describe(c4_fs_win32_kind(data.dwFileAttributes, tag), data.dwFileAttributes, data.nFileSizeHigh, data.nFileSizeLow, data.ftCreationTime,
		data.ftLastAccessTime, data.ftLastWriteTime, out);
	return true;
}

// What a link points to, through a handle the system opens at the end of the links.
static bool Followed(const wchar_t* path, c4_fs_file_info* out)
{
	HANDLE file = CreateFileW(path, FILE_READ_ATTRIBUTES, kShareAll, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (file == INVALID_HANDLE_VALUE) return false;
	BY_HANDLE_FILE_INFORMATION info;
	const bool read = GetFileInformationByHandle(file, &info) != 0;
	CloseHandle(file);
	if (read) Describe(c4_fs_win32_kind(info.dwFileAttributes & ~(DWORD)FILE_ATTRIBUTE_REPARSE_POINT, 0), info.dwFileAttributes, info.nFileSizeHigh,
		info.nFileSizeLow, info.ftCreationTime, info.ftLastAccessTime, info.ftLastWriteTime, out);
	return read;
}

int32_t c4_fs_info(const char* path, int32_t followLinks, c4_fs_file_info* out)
{
	memset(out, 0, sizeof(*out));
	wchar_t* wide = c4_utf8_to_wide(path);
	bool described = wide && !wcspbrk(wide, L"*?") && Itself(wide, out);
	if (described && followLinks && out->kind == kEntryLink) described = Followed(wide, out);
	free(wide);
	if (!described) memset(out, 0, sizeof(*out));
	return described ? 0 : -1;
}

int32_t c4_fs_set_modified(const char* path, int64_t unixNs)
{
	wchar_t* wide = c4_utf8_to_wide(path);
	HANDLE file = wide ? CreateFileW(wide, FILE_WRITE_ATTRIBUTES, kShareAll, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL) : INVALID_HANDLE_VALUE;
	free(wide);
	if (file == INVALID_HANDLE_VALUE) return -1;
	const FILETIME modified = FileTime(unixNs);
	const BOOL set = SetFileTime(file, NULL, NULL, &modified);
	CloseHandle(file);
	return set ? 0 : -1;
}

int32_t c4_fs_set_read_only(const char* path, int32_t readOnly)
{
	wchar_t* wide = c4_utf8_to_wide(path);
	const DWORD attributes = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
	const DWORD changed = readOnly ? attributes | FILE_ATTRIBUTE_READONLY : attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY;
	const BOOL set = attributes != INVALID_FILE_ATTRIBUTES && (changed == attributes || SetFileAttributesW(wide, changed));
	free(wide);
	return set ? 0 : -1;
}

int32_t c4_fs_copy_file(const char* from, const char* to, int32_t replace, char* error, int64_t errorCapacity)
{
	c4_copy_text("", error, errorCapacity);
	wchar_t* source = c4_utf8_to_wide(from);
	wchar_t* target = c4_utf8_to_wide(to);
	const BOOL copied = source && target && CopyFileW(source, target, replace ? FALSE : TRUE);
	const DWORD why = copied ? 0 : (source && target ? GetLastError() : ERROR_INVALID_NAME);
	free(source);
	free(target);
	if (copied) return 0;
	c4_system_error_text(why, error, errorCapacity);
	return -1;
}

// --- paths -----------------------------------------------------------------------------------------------------------

int64_t c4_fs_absolute(const char* path, char* out, int64_t capacity)
{
	wchar_t* wide = c4_utf8_to_wide(path);
	const DWORD needed = wide && *wide ? GetFullPathNameW(wide, 0, NULL, NULL) : 0;
	wchar_t* full = needed ? (wchar_t*)malloc(needed * sizeof(wchar_t)) : NULL;
	const DWORD length = full ? GetFullPathNameW(wide, needed, full, NULL) : 0;
	const int64_t written = length > 0 && length < needed ? c4_wide_to_utf8(full, out, capacity) : -1;
	free(full);
	free(wide);
	return written;
}

// "\\?\C:\a" as "C:\a" and "\\?\UNC\server\share" as "\\server\share": the final path without the prefix that turns
// off the system's parsing.
static const wchar_t* WithoutPrefix(wchar_t* path)
{
	if (wcsncmp(path, L"\\\\?\\UNC\\", 8) == 0)
	{
		path[6] = L'\\';
		return path + 6;
	}
	return wcsncmp(path, L"\\\\?\\", 4) == 0 ? path + 4 : path;
}

int64_t c4_fs_canonical(const char* path, char* out, int64_t capacity)
{
	wchar_t* wide = c4_utf8_to_wide(path);
	HANDLE file = wide && *wide ? CreateFileW(wide, 0, kShareAll, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL) : INVALID_HANDLE_VALUE;
	free(wide);
	if (file == INVALID_HANDLE_VALUE) return -1;
	const DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
	const DWORD needed = GetFinalPathNameByHandleW(file, NULL, 0, flags);
	wchar_t* final = needed ? (wchar_t*)malloc((needed + 1) * sizeof(wchar_t)) : NULL;
	const DWORD length = final ? GetFinalPathNameByHandleW(file, final, needed + 1, flags) : 0;
	CloseHandle(file);
	const int64_t written = length > 0 && length <= needed ? c4_wide_to_utf8(WithoutPrefix(final), out, capacity) : -1;
	free(final);
	return written;
}
