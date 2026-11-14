// Directories, rename and the current directory behind the fs_* builtins (Bedrock's FS::Directory and friends) on
// Linux. A file renamed onto another file system is copied there and then removed, as MoveFileEx's
// MOVEFILE_COPY_ALLOWED does on Windows; a folder cannot be (rename's EXDEV), as on Windows.

#include "FileSystemPosix.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int32_t c4_fs_is_directory(const char* path)
{
	struct stat status;
	return path && stat(path, &status) == 0 && S_ISDIR(status.st_mode) ? 1 : 0;
}

int32_t c4_fs_is_link(const char* path)
{
	struct stat status;
	return path && lstat(path, &status) == 0 && S_ISLNK(status.st_mode) ? 1 : 0;
}

int32_t c4_fs_make_directory(const char* path) { return path && mkdir(path, 0777) == 0 ? 0 : -1; }

int32_t c4_fs_remove_directory(const char* path) { return path && rmdir(path) == 0 ? 0 : -1; }

// A file moved to another file system: copied, then the source removed. As on Windows, the move has happened once the
// copy has, even when the source cannot be removed.
static int32_t MoveAcross(const char* from, const char* to)
{
	struct stat status;
	if (lstat(from, &status) != 0 || !S_ISREG(status.st_mode)) return -1;
	if (c4_fs_copy_file(from, to, 1, NULL, 0) != 0) return -1;
	unlink(from);
	return 0;
}

int32_t c4_fs_rename(const char* from, const char* to)
{
	if (!from || !to) return -1;
	if (rename(from, to) == 0) return 0;
	return errno == EXDEV ? MoveAcross(from, to) : -1;
}

// The folder that holds `path`, opened to be synced; -1 when it cannot be.
static int OpenFolderOf(const char* path)
{
	const char* slash = strrchr(path, '/');
	if (!slash) return open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	const size_t length = slash == path ? 1 : (size_t)(slash - path);
	char* folder = (char*)malloc(length + 1);
	if (!folder) return -1;
	memcpy(folder, path, length);
	folder[length] = 0;
	const int fd = open(folder, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	free(folder);
	return fd;
}

// The rename, then the folder that holds `to` synced, so the new name is on the disk when it returns. A file system
// that cannot sync a folder (EINVAL) has nothing more to write.
int32_t c4_fs_replace(const char* from, const char* to)
{
	if (!from || !to || rename(from, to) != 0) return -1;
	const int folder = OpenFolderOf(to);
	if (folder < 0) return -1;
	const int synced = fsync(folder) == 0 || errno == EINVAL ? 0 : -1;
	close(folder);
	return synced;
}

int64_t c4_fs_current_directory(char* out, int64_t cap)
{
	char* folder = getcwd(NULL, 0);
	const int64_t length = folder ? c4_fs_posix_text_out(folder, out, cap) : -1;
	free(folder);
	return length;
}

// TMPDIR, else /tmp.
int64_t c4_fs_temp_directory(char* out, int64_t cap)
{
	const char* folder = getenv("TMPDIR");
	return c4_fs_posix_text_out(folder && *folder ? folder : "/tmp", out, cap);
}

// A listing. `pending` is an entry a too-small buffer turned back, returned by the next call.
typedef struct C4Directory
{
	DIR* dir;
	struct dirent* pending;
} C4Directory;

void* c4_fs_directory_open(const char* path)
{
	DIR* opened = path && *path ? opendir(path) : NULL;
	if (!opened) return NULL;
	C4Directory* dir = (C4Directory*)calloc(1, sizeof(C4Directory));
	if (!dir)
	{
		closedir(opened);
		return NULL;
	}
	dir->dir = opened;
	return dir;
}

// The entry's kind from the listing, or from the entry itself where the file system does not say.
static int32_t EntryKind(C4Directory* dir, const struct dirent* entry)
{
	if (entry->d_type == DT_LNK) return kEntryLink;
	if (entry->d_type == DT_DIR) return kEntryDirectory;
	if (entry->d_type != DT_UNKNOWN) return kEntryFile;
	struct stat status;
	return fstatat(dirfd(dir->dir), entry->d_name, &status, AT_SYMLINK_NOFOLLOW) == 0 ? c4_fs_posix_kind(status.st_mode) : kEntryFile;
}

static int IsDotEntry(const char* name) { return name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)); }

// The next entry (not `.` or `..`): its name into `name` and its kind into `*kind`. Returns the name's length, 0 at
// the end, -1 when `name` is too small (the entry stays for the next call).
int32_t c4_fs_directory_next(void* handle, char* name, int32_t cap, int32_t* kind)
{
	C4Directory* dir = (C4Directory*)handle;
	if (!dir || !name || cap <= 0) return -1;
	for (;;)
	{
		struct dirent* entry = dir->pending ? dir->pending : readdir(dir->dir);
		dir->pending = NULL;
		if (!entry) return 0;
		if (IsDotEntry(entry->d_name)) continue;
		const size_t length = strlen(entry->d_name);
		if ((int64_t)length + 1 > cap)
		{
			dir->pending = entry;
			return -1;
		}
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
	free(dir);
}
