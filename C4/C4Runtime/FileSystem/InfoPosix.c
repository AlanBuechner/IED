// What is at a path, copies, and absolute and real paths for Bedrock's FS namespace (FileSystemRuntimeC.h) on Linux.
// The read-only attribute is the mode's write bits: a file is read-only when no one may write it; making it read-only
// clears every write bit, making it writable again gives its owner one. A file's creation time is its birth time
// where the file system keeps one (statx), else the earlier of its change and modification times.

#include "FileSystemPosix.h"
#include "SystemErrorC.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { kNsPerSecond = 1000000000 };

static const mode_t kWriteBits = S_IWUSR | S_IWGRP | S_IWOTH;
static const mode_t kPermissionBits = S_IRWXU | S_IRWXG | S_IRWXO;

static int64_t UnixNs(struct statx_timestamp time) { return (int64_t)time.tv_sec * kNsPerSecond + time.tv_nsec; }

// --- information -----------------------------------------------------------------------------------------------------

static void Describe(const struct statx* status, c4_fs_file_info* out)
{
	out->kind = c4_fs_posix_kind(status->stx_mode);
	out->readOnly = (status->stx_mode & kWriteBits) == 0;
	out->size = out->kind == kEntryFile ? (int64_t)status->stx_size : 0;
	out->modifiedNs = UnixNs(status->stx_mtime);
	out->accessedNs = UnixNs(status->stx_atime);
	const int64_t changedNs = UnixNs(status->stx_ctime);
	out->createdNs = (status->stx_mask & STATX_BTIME) ? UnixNs(status->stx_btime) : changedNs < out->modifiedNs ? changedNs : out->modifiedNs;
}

int32_t c4_fs_info(const char* path, int32_t followLinks, c4_fs_file_info* out)
{
	memset(out, 0, sizeof(*out));
	struct statx status;
	if (!path || statx(AT_FDCWD, path, followLinks ? 0 : AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_BTIME, &status) != 0) return -1;
	Describe(&status, out);
	return 0;
}

int32_t c4_fs_set_modified(const char* path, int64_t unixNs)
{
	int64_t seconds = unixNs / kNsPerSecond;
	int64_t ns = unixNs % kNsPerSecond;
	if (ns < 0)
	{
		seconds -= 1;
		ns += kNsPerSecond;
	}
	const struct timespec times[2] = { { 0, UTIME_OMIT }, { (time_t)seconds, (long)ns } };
	return path && utimensat(AT_FDCWD, path, times, 0) == 0 ? 0 : -1;
}

int32_t c4_fs_set_read_only(const char* path, int32_t readOnly)
{
	struct stat status;
	if (!path || stat(path, &status) != 0) return -1;
	const mode_t mode = status.st_mode & 07777;
	const mode_t changed = readOnly ? mode & ~kWriteBits : mode | S_IWUSR;
	return changed == mode || chmod(path, changed) == 0 ? 0 : -1;
}

// --- copies ----------------------------------------------------------------------------------------------------------

enum { kCopyChunkBytes = 1024 * 1024, kLargestRange = 0x40000000 };

static int WriteAll(int target, const char* bytes, size_t count)
{
	while (count > 0)
	{
		const ssize_t written = write(target, bytes, count);
		if (written < 0 && errno == EINTR) continue;
		if (written <= 0) return written < 0 ? errno : EIO;
		bytes += written;
		count -= (size_t)written;
	}
	return 0;
}

// The rest of `source` onto `target` by reading it. 0, or the errno that stopped it.
static int CopyByReading(int source, int target)
{
	char* buffer = (char*)malloc(kCopyChunkBytes);
	if (!buffer) return ENOMEM;
	int why = 0;
	for (;;)
	{
		const ssize_t got = read(source, buffer, kCopyChunkBytes);
		if (got < 0 && errno == EINTR) continue;
		if (got <= 0)
		{
			why = got < 0 ? errno : 0;
			break;
		}
		if ((why = WriteAll(target, buffer, (size_t)got)) != 0) break;
	}
	free(buffer);
	return why;
}

// `source` onto `target`: in the kernel (copy_file_range, which may share the blocks) for as long as it copies, then
// by reading whatever is left - all of it on a file system that cannot, or for a file that says it is empty (/proc).
static int CopyBytes(int source, int target, off_t size)
{
	off_t copied = 0;
	while (copied < size)
	{
		const off_t left = size - copied;
		const ssize_t moved = copy_file_range(source, NULL, target, NULL, left > kLargestRange ? kLargestRange : (size_t)left, 0);
		if (moved <= 0) break;
		copied += moved;
	}
	return CopyByReading(source, target);
}

// The target opened to be written from its start: made with the source's permissions, or (with `replace`) the file
// already there emptied - never the source itself, which emptying would lose. -1 with `*why`.
static int OpenTarget(const char* to, const struct stat* source, bool replace, int* why)
{
	const int target = open(to, O_WRONLY | O_CREAT | O_CLOEXEC | (replace ? 0 : O_EXCL), source->st_mode & kPermissionBits);
	struct stat status;
	*why = target < 0 ? errno : fstat(target, &status) != 0 ? errno : 0;
	if (!*why && status.st_dev == source->st_dev && status.st_ino == source->st_ino) *why = EINVAL;
	if (!*why && ftruncate(target, 0) != 0) *why = errno;
	if (!*why) return target;
	if (target >= 0) close(target);
	return -1;
}

// The source's permissions (the read-only attribute among them) and its access and modification times.
static int CopyAttributes(int target, const struct stat* source)
{
	const struct timespec times[2] = { source->st_atim, source->st_mtim };
	if (fchmod(target, source->st_mode & kPermissionBits) != 0) return errno;
	return futimens(target, times) == 0 ? 0 : errno;
}

// Why `status` is no file to copy: a folder, or a pipe or device, whose bytes have no end; 0 for a file.
static int NotAFile(const struct stat* status)
{
	if (S_ISDIR(status->st_mode)) return EISDIR;
	return S_ISREG(status->st_mode) ? 0 : EINVAL;
}

// 0, or the errno that stopped the copy. A target it opened and could not fill is removed, as CopyFileW does. The
// source opens without waiting, so a named pipe is refused rather than waited on.
static int CopyFile(const char* from, const char* to, bool replace)
{
	if (!from || !to) return EINVAL;
	const int source = open(from, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	if (source < 0) return errno;
	struct stat status;
	int why = fstat(source, &status) != 0 ? errno : NotAFile(&status);
	const int target = why ? -1 : OpenTarget(to, &status, replace, &why);
	if (target >= 0)
	{
		why = CopyBytes(source, target, status.st_size);
		if (!why) why = CopyAttributes(target, &status);
		if (close(target) != 0 && !why) why = errno;
		if (why) unlink(to);
	}
	close(source);
	return why;
}

int32_t c4_fs_copy_file(const char* from, const char* to, int32_t replace, char* error, int64_t errorCapacity)
{
	c4_copy_text("", error, errorCapacity);
	const int why = CopyFile(from, to, replace != 0);
	if (why == 0) return 0;
	c4_system_error_text((uint32_t)why, error, errorCapacity);
	return -1;
}

// --- paths -----------------------------------------------------------------------------------------------------------

// `path` after the working directory (malloc'd); null when either cannot be had.
static char* AfterCurrentDirectory(const char* path)
{
	char* folder = getcwd(NULL, 0);
	const size_t folderLength = folder ? strlen(folder) : 0;
	char* joined = folder ? (char*)malloc(folderLength + strlen(path) + 2) : NULL;
	if (joined)
	{
		memcpy(joined, folder, folderLength);
		joined[folderLength] = '/';
		strcpy(joined + folderLength + 1, path);
	}
	free(folder);
	return joined;
}

// An absolute path with "." and ".." and repeated slashes folded away, in place; ".." at the root stays there.
static void Fold(char* path)
{
	char* end = path;   // the folded path is [path, end)
	const char* read = path;
	while (*read)
	{
		while (*read == '/') ++read;
		const char* name = read;
		while (*read && *read != '/') ++read;
		const size_t length = (size_t)(read - name);
		if (length == 0 || (length == 1 && name[0] == '.')) continue;
		if (length == 2 && name[0] == '.' && name[1] == '.')
		{
			while (end > path && *--end != '/') {}
			continue;
		}
		*end++ = '/';
		memmove(end, name, length);
		end += length;
	}
	if (end == path) *end++ = '/';
	*end = 0;
}

int64_t c4_fs_absolute(const char* path, char* out, int64_t capacity)
{
	if (!path || !*path) return -1;
	char* absolute = path[0] == '/' ? strdup(path) : AfterCurrentDirectory(path);
	if (!absolute) return -1;
	Fold(absolute);
	const int64_t length = c4_fs_posix_text_out(absolute, out, capacity);
	free(absolute);
	return length;
}

int64_t c4_fs_canonical(const char* path, char* out, int64_t capacity)
{
	char* real = path && *path ? realpath(path, NULL) : NULL;
	if (!real) return -1;
	const int64_t length = c4_fs_posix_text_out(real, out, capacity);
	free(real);
	return length;
}
