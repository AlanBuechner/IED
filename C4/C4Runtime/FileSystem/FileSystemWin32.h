#pragma once
// What the Win32 file-system files share.

#include "FileSystemRuntimeC.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// An entry's kind from its attributes and, for a reparse point, its tag. Only symbolic links and junctions are links:
// other reparse points (cloud placeholders, deduplicated files) are the files and folders they stand for.
static inline int32_t c4_fs_win32_kind(DWORD attributes, DWORD reparseTag)
{
	const BOOL link = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) && (reparseTag == IO_REPARSE_TAG_SYMLINK || reparseTag == IO_REPARSE_TAG_MOUNT_POINT);
	if (link) return kEntryLink;
	return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? kEntryDirectory : kEntryFile;
}

// The reparse tag of the entry at `path` (0 when it has none or cannot be read).
static inline DWORD c4_fs_win32_reparse_tag(const wchar_t* path)
{
	WIN32_FIND_DATAW data;
	HANDLE found = FindFirstFileW(path, &data);
	if (found == INVALID_HANDLE_VALUE) return 0;
	FindClose(found);
	return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? data.dwReserved0 : 0;
}
