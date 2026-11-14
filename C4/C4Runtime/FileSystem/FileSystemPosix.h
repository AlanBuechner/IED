#pragma once
// What the POSIX file-system files share.

#include "FileSystemRuntimeC.h"
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

// An entry's kind from its mode: only a symbolic link is a link; devices, pipes and sockets are files.
static inline int32_t c4_fs_posix_kind(mode_t mode)
{
	if (S_ISLNK(mode)) return kEntryLink;
	return S_ISDIR(mode) ? kEntryDirectory : kEntryFile;
}

// `text` into `out` as the runtime hands text back: its length, written (null-terminated) only when it fits.
static inline int64_t c4_fs_posix_text_out(const char* text, char* out, int64_t capacity)
{
	const int64_t length = (int64_t)strlen(text);
	if (out && capacity > length) memcpy(out, text, (size_t)length + 1);
	return length;
}
