// Watching a folder for Bedrock's FS::Watcher (FileSystemRuntimeC.h) with inotify, which watches one folder at a time:
// a recursive watch adds one for every folder under the watched one when it opens, and one for each folder that
// appears later (made, or moved in), whose entries at that moment are reported as Added, so nothing made in it before
// its watch was in place is missed. A rename is two events with one cookie, the old name then the new, which become one
// change; an old name with no new name after it is an entry that left the watched folder, a new name alone one that
// came in. Folders are reached through a descriptor of the watched one (/proc/self/fd), so watching goes on when the
// watched folder itself is renamed or moved, as on Windows; while that descriptor is open the kernel does not report
// the folder deleted, so a wait looks at its link count, at least every kRootCheckMs. Each watch is an inotify
// instance of its own: the system limits those per user (fs.inotify.max_user_instances) as it limits watched folders
// (fs.inotify.max_user_watches).

#include "FileSystemPosix.h"
#include "SystemErrorC.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <time.h>
#include <unistd.h>

enum { kWatchBufferBytes = 64 * 1024, kFailureBytes = 256, kRenamePairMs = 5, kRootCheckMs = 500, kFirstFolders = 16 };

static const char* const kRootRemoved = "the watched folder was removed";

static const uint32_t kFolderEvents = IN_CREATE | IN_DELETE | IN_MODIFY | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF |
	IN_ONLYDIR | IN_EXCL_UNLINK;

// A growable string a change's path is handed out in.
typedef struct Text { char* bytes; size_t capacity; } Text;

// Paths found by looking into a new folder, handed out as Added before the events after it.
typedef struct Found { char** paths; size_t count; size_t next; size_t capacity; } Found;

typedef struct Watch
{
	int inotify;
	int root;                    // the watched folder (O_PATH)
	int rootWd;
	bool recursive;
	char** folders;              // each watched folder's path relative to the watched one, by watch descriptor
	int folderCapacity;
	char* events;                // what the last read returned; [next, filled) is not handed out yet
	size_t filled;
	size_t next;
	Found found;
	char failure[kFailureBytes]; // why watching ended; empty while it goes on
	Text path;
	Text oldPath;
	bool hasOld;                 // oldPath holds a rename's old name, waiting for the new one
	uint32_t oldCookie;
	bool oldIsFolder;
} Watch;

// --- text ------------------------------------------------------------------------------------------------------------

static bool Store(Text* text, const char* value)
{
	const size_t needed = strlen(value) + 1;
	if (needed > text->capacity)
	{
		char* grown = (char*)realloc(text->bytes, needed);
		if (!grown) return false;
		text->bytes = grown;
		text->capacity = needed;
	}
	memcpy(text->bytes, value, needed);
	return true;
}

static const char* TextOf(const Text* text) { return text->bytes ? text->bytes : ""; }

// `folder`/`name`, or `name` alone in the watched folder itself (malloc'd).
static char* Join(const char* folder, const char* name)
{
	const size_t folderLength = strlen(folder);
	const size_t nameLength = strlen(name);
	char* path = (char*)malloc(folderLength + nameLength + 2);
	if (!path) return NULL;
	if (folderLength) memcpy(path, folder, folderLength);
	if (folderLength) path[folderLength] = '/';
	memcpy(path + folderLength + (folderLength ? 1 : 0), name, nameLength + 1);
	return path;
}

// Whether `path` is `folder` or under it.
static bool IsWithin(const char* path, const char* folder, size_t folderLength)
{
	return strncmp(path, folder, folderLength) == 0 && (path[folderLength] == 0 || path[folderLength] == '/');
}

// Why watching failed, in words: the system's, or for its limits the setting that raises them.
static void ErrorText(int code, char* out, int64_t capacity)
{
	if (code == ENOSPC) c4_copy_text("the system's limit of watched folders is reached (fs.inotify.max_user_watches)", out, capacity);
	else if (code == EMFILE) c4_copy_text("the system's limit of folder watches (fs.inotify.max_user_instances) or of open files is reached", out, capacity);
	else c4_system_error_text((uint32_t)code, out, capacity);
}

static void Fail(Watch* watch, int code) { ErrorText(code, watch->failure, kFailureBytes); }

// --- the folders watched ---------------------------------------------------------------------------------------------

static const char* FolderPath(const Watch* watch, int wd) { return wd >= 0 && wd < watch->folderCapacity ? watch->folders[wd] : NULL; }

// Keeps `path` (taken) as the folder watched by `wd`. 0 or ENOMEM.
static int Remember(Watch* watch, int wd, char* path)
{
	if (wd >= watch->folderCapacity)
	{
		int capacity = watch->folderCapacity ? watch->folderCapacity : kFirstFolders;
		while (capacity <= wd) capacity *= 2;
		char** grown = (char**)realloc(watch->folders, (size_t)capacity * sizeof(char*));
		if (!grown)
		{
			free(path);
			return ENOMEM;
		}
		memset(grown + watch->folderCapacity, 0, (size_t)(capacity - watch->folderCapacity) * sizeof(char*));
		watch->folders = grown;
		watch->folderCapacity = capacity;
	}
	free(watch->folders[wd]);
	watch->folders[wd] = path;
	return 0;
}

static void Forget(Watch* watch, int wd)
{
	if (!FolderPath(watch, wd)) return;
	free(watch->folders[wd]);
	watch->folders[wd] = NULL;
}

// Adds the system's watch of the folder at `path` (relative; "" is the watched folder, whose link is followed).
// 0, or the errno.
static int AddWatch(Watch* watch, const char* path, int* wd)
{
	char* at = (char*)malloc(strlen(path) + 48);
	if (!at) return ENOMEM;
	sprintf(at, *path ? "/proc/self/fd/%d/%s" : "/proc/self/fd/%d", watch->root, path);
	*wd = inotify_add_watch(watch->inotify, at, kFolderEvents | (*path ? IN_DONT_FOLLOW : 0));
	free(at);
	return *wd < 0 ? errno : 0;
}

// Watches the folder at `path`, under the watched one. 0, or the errno; a folder gone already, or one this process
// may not read, has nothing to watch.
static int WatchFolder(Watch* watch, const char* path)
{
	int wd = -1;
	const int why = AddWatch(watch, path, &wd);
	if (why == ENOENT || why == ENOTDIR || why == EACCES || why == EPERM) return 0;
	if (why) return why;
	char* kept = strdup(path);
	return kept ? Remember(watch, wd, kept) : ENOMEM;
}

static bool IsFolder(DIR* dir, const struct dirent* entry)
{
	if (entry->d_type != DT_UNKNOWN) return entry->d_type == DT_DIR;
	struct stat status;
	return fstatat(dirfd(dir), entry->d_name, &status, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(status.st_mode);
}

static int Queue(Found* found, const char* path)
{
	if (found->count == found->capacity)
	{
		const size_t capacity = found->capacity ? found->capacity * 2 : kFirstFolders;
		char** grown = (char**)realloc(found->paths, capacity * sizeof(char*));
		if (!grown) return ENOMEM;
		found->paths = grown;
		found->capacity = capacity;
	}
	char* copy = strdup(path);
	if (!copy) return ENOMEM;
	found->paths[found->count++] = copy;
	return 0;
}

// Watches every folder under the folder at `path` (itself watched already); with `report`, every entry under it is
// queued as Added, a folder before what it holds. 0, or the errno that stopped it.
static int WatchUnder(Watch* watch, const char* path, bool report)
{
	const int fd = openat(watch->root, *path ? path : ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	DIR* dir = fd >= 0 ? fdopendir(fd) : NULL;
	if (!dir)
	{
		if (fd >= 0) close(fd);
		return 0;   // gone already, or not readable: nothing in it to watch
	}
	int why = 0;
	for (struct dirent* entry; !why && (entry = readdir(dir)) != NULL;)
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
		const bool folder = IsFolder(dir, entry);
		char* inner = Join(path, entry->d_name);
		if (!inner) why = ENOMEM;
		if (!why && report) why = Queue(&watch->found, inner);
		if (!why && folder) why = WatchFolder(watch, inner);
		if (!why && folder) why = WatchUnder(watch, inner, report);
		free(inner);
	}
	closedir(dir);
	return why;
}

// Stops watching the folder at `path` and every one under it: it left the watched folder.
static void ForgetTree(Watch* watch, const char* path)
{
	const size_t length = strlen(path);
	for (int wd = 0; wd < watch->folderCapacity; ++wd)
		if (watch->folders[wd] && wd != watch->rootWd && IsWithin(watch->folders[wd], path, length))
		{
			inotify_rm_watch(watch->inotify, wd);
			Forget(watch, wd);
		}
}

// The folder at `from` and every one under it, now at `to`: a rename inside the watched folder.
static int MoveTree(Watch* watch, const char* from, const char* to)
{
	const size_t length = strlen(from);
	for (int wd = 0; wd < watch->folderCapacity; ++wd)
	{
		const char* folder = watch->folders[wd];
		if (!folder || !IsWithin(folder, from, length)) continue;
		char* moved = (char*)malloc(strlen(to) + strlen(folder + length) + 1);
		if (!moved) return ENOMEM;
		strcpy(moved, to);
		strcat(moved, folder + length);
		Remember(watch, wd, moved);
	}
	return 0;
}

// A folder that appeared under the watched one, with what it holds already.
static void WatchNewFolder(Watch* watch, const char* path)
{
	int why = WatchFolder(watch, path);
	if (!why) why = WatchUnder(watch, path, true);
	if (why) Fail(watch, why);
}

// --- handing out events ----------------------------------------------------------------------------------------------

static void Hand(c4_fs_change* out, int32_t kind, const char* path, const char* oldPath)
{
	out->kind = kind;
	out->path = path;
	out->oldPath = oldPath;
}

// A rename's old name with no new name after it: the entry left the watched folder.
static void LoneOldName(Watch* watch, c4_fs_change* out)
{
	watch->hasOld = false;
	if (watch->oldIsFolder && watch->recursive) ForgetTree(watch, TextOf(&watch->oldPath));
	Hand(out, kFsRemoved, TextOf(&watch->oldPath), "");
}

// Events were lost. Folders made meanwhile are looked for, so what changes in them from now on is seen.
static void Overflowed(Watch* watch, c4_fs_change* out)
{
	watch->hasOld = false;
	const int why = watch->recursive ? WatchUnder(watch, "", false) : 0;
	if (why) Fail(watch, why);
	Hand(out, kFsOverflow, "", "");
}

static int32_t KindOf(uint32_t mask)
{
	if (mask & (IN_CREATE | IN_MOVED_TO)) return kFsAdded;
	return (mask & IN_DELETE) ? kFsRemoved : kFsModified;
}

// The change an event at `path` makes, into `out`; false for a rename's old name, which waits for its new name.
static bool TranslateAt(Watch* watch, const struct inotify_event* event, const char* path, c4_fs_change* out)
{
	const bool folder = (event->mask & IN_ISDIR) != 0;
	if (event->mask & IN_MOVED_FROM)
	{
		watch->hasOld = Store(&watch->oldPath, path);
		watch->oldCookie = event->cookie;
		watch->oldIsFolder = folder;
		if (!watch->hasOld) Fail(watch, ENOMEM);
		return false;
	}
	if (!Store(&watch->path, path))
	{
		Fail(watch, ENOMEM);
		return false;
	}
	if ((event->mask & IN_MOVED_TO) && watch->hasOld)
	{
		watch->hasOld = false;
		if (folder && MoveTree(watch, TextOf(&watch->oldPath), path) != 0) Fail(watch, ENOMEM);
		Hand(out, kFsRenamed, TextOf(&watch->path), TextOf(&watch->oldPath));
		return true;
	}
	const int32_t kind = KindOf(event->mask);
	if (kind == kFsAdded && folder && watch->recursive) WatchNewFolder(watch, path);
	Hand(out, kind, TextOf(&watch->path), "");
	return true;
}

// The change an event makes, into `out`; false when it makes none to hand out.
static bool Translate(Watch* watch, const struct inotify_event* event, c4_fs_change* out)
{
	if (event->mask & IN_Q_OVERFLOW)
	{
		Overflowed(watch, out);
		return true;
	}
	if (event->wd == watch->rootWd && (event->mask & (IN_DELETE_SELF | IN_IGNORED | IN_UNMOUNT)))
	{
		c4_copy_text(kRootRemoved, watch->failure, kFailureBytes);
		return false;
	}
	if (event->mask & IN_IGNORED)
	{
		Forget(watch, event->wd);
		return false;
	}
	// A folder no longer watched, or a change to a folder itself, which its parent's watch reports.
	const char* folder = FolderPath(watch, event->wd);
	if (!folder || event->len == 0 || !event->name[0]) return false;
	char* path = Join(folder, event->name);
	if (!path)
	{
		Fail(watch, ENOMEM);
		return false;
	}
	const bool translated = TranslateAt(watch, event, path, out);
	free(path);
	return translated;
}

static const struct inotify_event* NextEvent(const Watch* watch)
{
	return watch->next < watch->filled ? (const struct inotify_event*)(watch->events + watch->next) : NULL;
}

// The next change already received; false when there is none.
static bool Take(Watch* watch, c4_fs_change* out)
{
	Found* found = &watch->found;
	if (found->next < found->count)
	{
		char* path = found->paths[found->next++];
		const bool stored = Store(&watch->path, path);
		free(path);
		if (found->next == found->count) found->next = found->count = 0;
		if (stored) Hand(out, kFsAdded, TextOf(&watch->path), "");
		else Fail(watch, ENOMEM);
		return stored;
	}
	for (const struct inotify_event* event; (event = NextEvent(watch)) != NULL;)
	{
		if (watch->hasOld && !((event->mask & IN_MOVED_TO) && event->cookie == watch->oldCookie))
		{
			LoneOldName(watch, out);
			return true;
		}
		watch->next += sizeof(struct inotify_event) + event->len;
		if (Translate(watch, event, out)) return true;
	}
	return false;
}

// --- waiting ---------------------------------------------------------------------------------------------------------

static int64_t NowMs(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

// When a wait of `timeoutMs` ends; -1 for never.
static int64_t Deadline(int32_t timeoutMs) { return timeoutMs < 0 ? -1 : NowMs() + timeoutMs; }

// Milliseconds left until `deadline`, at least 0; -1 for no deadline.
static int32_t Remaining(int64_t deadline)
{
	if (deadline < 0) return -1;
	const int64_t left = deadline - NowMs();
	return left > 0 ? (int32_t)left : 0;
}

// Whether the watched folder was removed: its last link is gone, though this watch's descriptor keeps it from going.
static bool RootRemoved(const Watch* watch)
{
	struct stat status;
	return fstat(watch->root, &status) == 0 && status.st_nlink == 0;
}

// Waits up to `timeoutMs` for events and reads them: 1, kFsTimedOut, or -1 with the failure kept.
static int32_t Receive(Watch* watch, int32_t timeoutMs)
{
	const int64_t deadline = Deadline(timeoutMs);
	for (;;)
	{
		const ssize_t got = read(watch->inotify, watch->events, kWatchBufferBytes);
		if (got > 0)
		{
			watch->filled = (size_t)got;
			watch->next = 0;
			return 1;
		}
		if (got == 0 || (errno != EAGAIN && errno != EINTR))
		{
			Fail(watch, got == 0 ? EIO : errno);
			return -1;
		}
		if (RootRemoved(watch))
		{
			c4_copy_text(kRootRemoved, watch->failure, kFailureBytes);
			return -1;
		}
		const int32_t left = Remaining(deadline);
		if (left == 0) return kFsTimedOut;
		struct pollfd ready = { watch->inotify, POLLIN, 0 };
		if (poll(&ready, 1, left < 0 || left > kRootCheckMs ? kRootCheckMs : left) < 0 && errno != EINTR)
		{
			Fail(watch, errno);
			return -1;
		}
	}
}

// --- the watch -------------------------------------------------------------------------------------------------------

static void CloseWatch(Watch* watch)
{
	if (watch->inotify >= 0) close(watch->inotify);
	if (watch->root >= 0) close(watch->root);
	for (int wd = 0; wd < watch->folderCapacity; ++wd) free(watch->folders[wd]);
	free(watch->folders);
	for (size_t i = watch->found.next; i < watch->found.count; ++i) free(watch->found.paths[i]);
	free(watch->found.paths);
	free(watch->events);
	free(watch->path.bytes);
	free(watch->oldPath.bytes);
	free(watch);
}

// 0, or the errno that stopped it.
static int Open(Watch* watch, const char* path, bool recursive)
{
	watch->recursive = recursive;
	watch->events = (char*)malloc(kWatchBufferBytes);
	if (!watch->events) return ENOMEM;
	if ((watch->root = open(path, O_PATH | O_DIRECTORY | O_CLOEXEC)) < 0) return errno;
	if ((watch->inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) < 0) return errno;
	int why = AddWatch(watch, "", &watch->rootWd);
	char* itself = why ? NULL : strdup("");
	if (!why) why = itself ? Remember(watch, watch->rootWd, itself) : ENOMEM;
	if (!why && recursive) why = WatchUnder(watch, "", false);
	return why;
}

void* c4_fs_watch_open(const char* path, int32_t recursive, char* error, int64_t errorCapacity)
{
	c4_copy_text("", error, errorCapacity);
	Watch* watch = (Watch*)calloc(1, sizeof(Watch));
	if (watch)
	{
		watch->inotify = -1;
		watch->root = -1;
		watch->rootWd = -1;
	}
	const int why = !watch ? ENOMEM : !path || !*path ? ENOENT : Open(watch, path, recursive != 0);
	if (!why) return watch;
	ErrorText(why, error, errorCapacity);
	if (watch) CloseWatch(watch);
	return NULL;
}

int32_t c4_fs_watch_next(void* handle, int32_t timeoutMs, c4_fs_change* out, char* error, int64_t errorCapacity)
{
	c4_copy_text("", error, errorCapacity);
	Watch* watch = (Watch*)handle;
	if (!watch || !out) return -1;
	const int64_t deadline = Deadline(timeoutMs);
	while (!Take(watch, out))
	{
		if (watch->failure[0])
		{
			c4_copy_text(watch->failure, error, errorCapacity);
			return -1;
		}
		// An old name waits a moment for its new name, which the system may not have added yet.
		const bool pairing = watch->hasOld;
		const int32_t received = Receive(watch, pairing ? kRenamePairMs : Remaining(deadline));
		if (received == kFsTimedOut && pairing) LoneOldName(watch, out);
		if (received == kFsTimedOut) return pairing ? 1 : kFsTimedOut;
		if (received < 0)
		{
			c4_copy_text(watch->failure, error, errorCapacity);
			return -1;
		}
	}
	return 1;
}

void c4_fs_watch_close(void* watch)
{
	if (watch) CloseWatch((Watch*)watch);
}
