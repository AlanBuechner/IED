// C4 file I/O runtime - thin stdio-backed helpers linked into every executable (see
// EnsureIORuntimeObject in BuildUtils). Backs the `file_*` builtins registered in BuiltinDeclarations.
//
// Design:
//   - An open file is a void* FILE* handed back to C4 as an opaque handle. It is NOT GC-managed
//     (FILE* is not a heap arena pointer), so callers keep it in a top-level local or field; the
//     conservative stack/global scan leaves foreign pointers alone and c4_free rejects them.
//   - read/write return the number of bytes actually transferred, or -1 on error/invalid handle.
//   - stdio (fopen/fread/fwrite/fseek/ftell) is used so the layer stays portable: Win32 today,
//     POSIX later with no API change to C4 code.
//   - Modes are passed through as C mode strings: "r" read, "w" write(truncate), "a" append,
//     and the binary forms "rb"/"wb"/"ab".
//   - Paths are UTF-8; on Windows they go to the wide CRT calls (PathRuntimeC.h).

#include "PathRuntimeC.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>

// 64-bit stream positions: `long` is 32 bits on Windows, so fseek/ftell stop at 2 GiB.
#ifdef _WIN32
#define c4_fseek _fseeki64
#define c4_ftell _ftelli64
typedef struct _stat64 c4_stat_t;
#else
#define c4_fseek fseeko
#define c4_ftell ftello
typedef struct stat c4_stat_t;
#endif

static FILE* handle(FILE** h)
{
	return (h && *h) ? *h : NULL;
}

#ifdef _WIN32
static FILE* OpenPath(const char* path, const char* mode)
{
	wchar_t* widePath = c4_utf8_to_wide(path);
	wchar_t* wideMode = c4_utf8_to_wide(mode);
	FILE* f = widePath && wideMode ? _wfopen(widePath, wideMode) : NULL;
	free(widePath);
	free(wideMode);
	return f;
}

static int StatPath(const char* path, c4_stat_t* st)
{
	wchar_t* wide = c4_utf8_to_wide(path);
	int r = wide ? _wstat64(wide, st) : -1;
	free(wide);
	return r;
}

static int RemovePath(const char* path)
{
	wchar_t* wide = c4_utf8_to_wide(path);
	int r = wide ? _wremove(wide) : -1;
	free(wide);
	return r;
}
#else
static FILE* OpenPath(const char* path, const char* mode) { return fopen(path, mode); }
static int StatPath(const char* path, c4_stat_t* st) { return stat(path, st); }
static int RemovePath(const char* path) { return remove(path); }
#endif

// Open a file. Returns null on failure (missing file for "r", permissions, bad path).
void* file_open(const char* path, const char* mode)
{
	if (!path || !mode) return (void*)0;
	return (void*)OpenPath(path, mode);
}

// Read up to `len` bytes into `buf`. Returns bytes read (>0), 0 at EOF, or -1 on error.
int64_t file_read(void* h, void* buf, int64_t len)
{
	FILE* f = handle((FILE**)&h);
	if (!f || !buf || len <= 0) return -1;
	size_t got = fread(buf, 1, (size_t)len, f);
	if (got == 0 && ferror(f)) return -1;
	return (int64_t)got;
}

// Write `len` bytes from `buf`. Returns bytes written (>0) or -1 on error.
int64_t file_write(void* h, const void* buf, int64_t len)
{
	FILE* f = handle((FILE**)&h);
	if (!f || !buf || len <= 0) return -1;
	size_t n = fwrite(buf, 1, (size_t)len, f);
	if (n != (size_t)len && ferror(f)) return -1;
	return (int64_t)n;
}

// Flush the file's buffers. Returns 0 on success or -1 on error.
int32_t file_flush(void* h)
{
	FILE* f = handle((FILE**)&h);
	if (!f) return -1;
	int r = fflush(f);
	return (r == 0) ? 0 : -1;
}

// Close the file, flushing first. Returns 0 on success or -1 on error.
int32_t file_close(void* h)
{
	FILE* f = handle((FILE**)&h);
	if (!f) return -1;
	int r = fclose(f);
	return (r == 0) ? 0 : -1;
}

// Move to `offset` bytes from `origin` (0 the start, 1 the current position, 2 the end). Returns the new
// position from the start, or -1 on error.
int64_t file_seek_from(void* h, int64_t offset, int32_t origin)
{
	FILE* f = handle((FILE**)&h);
	if (!f || origin < 0 || origin > 2) return -1;
	static const int kWhence[3] = { SEEK_SET, SEEK_CUR, SEEK_END };
	if (c4_fseek(f, offset, kWhence[origin]) != 0) return -1;
	int64_t p = c4_ftell(f);
	return (p < 0) ? -1 : p;
}

// Move to `offset` bytes from the start. Returns the new position, or -1 on error.
int64_t file_seek(void* h, int64_t offset)
{
	return file_seek_from(h, offset, 0);
}

// Current stream position in bytes from the start WITHOUT moving it, or -1 on error.
int64_t file_tell(void* h)
{
	FILE* f = handle((FILE**)&h);
	if (!f) return -1;
	int64_t p = c4_ftell(f);
	return (p < 0) ? -1 : p;
}

// Size of the regular file at path in bytes, or -1 when there is none.
int64_t file_size(const char* path)
{
	c4_stat_t st;
	if (!path || StatPath(path, &st) != 0 || !(st.st_mode & S_IFREG)) return -1;
	return (int64_t)st.st_size;
}

// True when the path exists and is a regular file.
int32_t file_exists(const char* path)
{
	c4_stat_t st;
	return path && StatPath(path, &st) == 0 && (st.st_mode & S_IFREG) ? 1 : 0;
}

// Delete the file at path. Returns 0 on success, -1 if it does not exist or removal fails.
int32_t file_delete(const char* path)
{
	if (!path) return -1;
	return RemovePath(path) == 0 ? 0 : -1;
}

// Writes `count` bytes to the process's standard output (stream 1) or error (stream 2) and flushes them, through the
// C runtime's own FILE, so they stay in order with printf. Returns the bytes written, or -1 for another stream or a
// failed write.
int64_t c4_console_write(int32_t stream, const void* bytes, int64_t count)
{
	FILE* f = stream == 1 ? stdout : stream == 2 ? stderr : NULL;
	if (!f || count < 0 || (count > 0 && !bytes)) return -1;
	const size_t written = count ? fwrite(bytes, 1, (size_t)count, f) : 0;
	if (fflush(f) != 0 || written != (size_t)count) return -1;
	return (int64_t)written;
}
