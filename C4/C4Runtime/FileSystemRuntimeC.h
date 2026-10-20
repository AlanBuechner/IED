#pragma once
// The file system behind Bedrock's FS namespace beyond the fs_* builtins (D-69): what is at a path, copies, absolute
// and real paths, and watching a folder for changes. One implementation per OS (FileSystem/*Win32.c). Paths are
// UTF-8 and come back as the system writes them.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bedrock's FS::EntryKind. A link is a symbolic link or a junction.
enum { kEntryFile = 0, kEntryDirectory = 1, kEntryLink = 2 };

// A wait that gave up (c4_fs_watch_next).
enum { kFsTimedOut = -2 };

// Bedrock's FS::FileInfo. Times are Unix time in nanoseconds (Platform::UnixTimeNs's clock).
typedef struct c4_fs_file_info
{
	int32_t kind;
	int32_t readOnly;
	int64_t size;         // bytes; 0 for a directory
	int64_t modifiedNs;
	int64_t createdNs;
	int64_t accessedNs;
} c4_fs_file_info;

// What is at `path`: 0 with `out` filled, -1 when nothing is there or it cannot be read. A link is followed to what it
// points to unless `followLinks` is 0; then it is described itself, as a Link.
int32_t c4_fs_info(const char* path, int32_t followLinks, c4_fs_file_info* out);
// Sets a file's or folder's modification time. 0 or -1.
int32_t c4_fs_set_modified(const char* path, int64_t unixNs);
// Makes a file read-only, or writable again. 0 or -1.
int32_t c4_fs_set_read_only(const char* path, int32_t readOnly);
// Copies a file with its attributes; with `replace` 0 it fails when `to` exists. 0, or -1 with `error`.
int32_t c4_fs_copy_file(const char* from, const char* to, int32_t replace, char* error, int64_t errorCapacity);
// `path` made absolute against the working directory, "." and ".." folded, without touching the file system. The
// UTF-8 length, written (null-terminated) only when it fits in `capacity`; -1 when it cannot be made.
int64_t c4_fs_absolute(const char* path, char* out, int64_t capacity);
// The path the file system resolves `path` to: absolute, links followed, each name in its stored case. Its length as
// c4_fs_absolute gives it; -1 when nothing is there.
int64_t c4_fs_canonical(const char* path, char* out, int64_t capacity);

// Bedrock's FS::ChangeKind. Overflow: changes were lost (too many at once); look at the folder again.
enum { kFsAdded = 0, kFsRemoved = 1, kFsModified = 2, kFsRenamed = 3, kFsOverflow = 4 };

// One change under a watched folder. The paths are relative to it, valid until the next call on the watch.
typedef struct c4_fs_change
{
	int32_t kind;
	const char* path;
	const char* oldPath;   // a rename's old path; "" otherwise
} c4_fs_change;

// Starts watching the folder at `path` (and everything under it with `recursive`). The watch, or null with `error`.
void* c4_fs_watch_open(const char* path, int32_t recursive, char* error, int64_t errorCapacity);
// The next change: 1 with `out` filled, kFsTimedOut, or -1 with `error` (the folder is gone, or watching failed). A
// change made after c4_fs_watch_open returned is never missed: it comes, or an Overflow in its place.
int32_t c4_fs_watch_next(void* watch, int32_t timeoutMs, c4_fs_change* out, char* error, int64_t errorCapacity);
void c4_fs_watch_close(void* watch);

#ifdef __cplusplus
}
#endif
