// The processes c4_process_kill ends (ProcessPosix.h), found in /proc: the child, every process below it by parent
// links, and with `session` every process in its session, which the child leads, so its id names its session and its
// process group too. They are stopped first, round after round until a round finds none new, so none can start
// another between being found and being ended; then all of them are killed. Only a process that left both the session
// and the tree (setsid, its parent gone: a daemon) is out of reach, where a Windows job would still hold it.

#include "ProcessPosix.h"
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { kMostRounds = 16, kStatBytes = 1024, kFirstIds = 64 };

typedef struct Process { pid_t id; pid_t parent; pid_t session; } Process;
typedef struct Processes { Process* items; size_t count; size_t capacity; } Processes;
typedef struct Ids { pid_t* items; size_t count; size_t capacity; } Ids;

static bool Grow(void** items, size_t* capacity, size_t count, size_t itemBytes)
{
	if (count < *capacity) return true;
	const size_t grown = *capacity ? *capacity * 2 : kFirstIds;
	void* larger = realloc(*items, grown * itemBytes);
	if (!larger) return false;
	*items = larger;
	*capacity = grown;
	return true;
}

static bool Contains(const Ids* ids, pid_t id)
{
	for (size_t i = 0; i < ids->count; ++i)
		if (ids->items[i] == id) return true;
	return false;
}

static bool Push(Ids* ids, pid_t id)
{
	if (!Grow((void**)&ids->items, &ids->capacity, ids->count, sizeof(pid_t))) return false;
	ids->items[ids->count++] = id;
	return true;
}

// A process's parent and session from /proc/<id>/stat. Its second field, the name in parentheses, may hold anything,
// so the rest is read after its last ')'.
static bool ReadProcess(pid_t id, Process* out)
{
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/stat", (int)id);
	FILE* file = fopen(path, "re");
	if (!file) return false;
	char text[kStatBytes];
	const size_t length = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[length] = 0;
	const char* fields = strrchr(text, ')');
	char state = 0;
	int parent = 0;
	int group = 0;
	int session = 0;
	if (!fields || sscanf(fields + 1, " %c %d %d %d", &state, &parent, &group, &session) != 4) return false;
	out->id = id;
	out->parent = parent;
	out->session = session;
	return true;
}

// Every process the system lists now; as many as could be read.
static void ReadProcesses(Processes* out)
{
	DIR* proc = opendir("/proc");
	if (!proc) return;
	for (struct dirent* entry; (entry = readdir(proc)) != NULL;)
	{
		char* end = NULL;
		const long id = strtol(entry->d_name, &end, 10);
		Process process;
		if (end == entry->d_name || *end || id <= 0 || !ReadProcess((pid_t)id, &process)) continue;
		if (!Grow((void**)&out->items, &out->capacity, out->count, sizeof(Process))) break;
		out->items[out->count++] = process;
	}
	closedir(proc);
}

// `root`, the processes below it, and with `session` those in its session, into `found`.
static void Find(pid_t root, bool session, const Processes* all, Ids* found)
{
	if (!Push(found, root)) return;
	for (bool grew = true; grew;)
	{
		grew = false;
		for (size_t i = 0; i < all->count; ++i)
		{
			const Process* process = &all->items[i];
			const bool belongs = Contains(found, process->parent) || (session && process->session == root);
			if (!belongs || Contains(found, process->id)) continue;
			if (!Push(found, process->id)) return;
			grew = true;
		}
	}
}

// One round: every process Find gives now is stopped, and those not in `stopped` are added to it. Whether there were
// any such.
static bool StopRound(pid_t root, bool session, Ids* stopped)
{
	Processes all = { 0 };
	Ids found = { 0 };
	ReadProcesses(&all);
	Find(root, session, &all, &found);
	if (session) kill(-root, SIGSTOP);
	bool fresh = false;
	for (size_t i = 0; i < found.count; ++i)
	{
		const pid_t id = found.items[i];
		if (id == getpid() || Contains(stopped, id)) continue;
		kill(id, SIGSTOP);
		fresh = Push(stopped, id) || fresh;
	}
	free(all.items);
	free(found.items);
	return fresh;
}

bool c4_process_posix_kill_tree(pid_t root, bool session)
{
	Ids stopped = { 0 };
	for (int round = 0; round < kMostRounds && StopRound(root, session, &stopped); ++round) {}
	if (session) kill(-root, SIGKILL);
	for (size_t i = 0; i < stopped.count; ++i) kill(stopped.items[i], SIGKILL);
	const bool signalled = kill(root, SIGKILL) == 0 || errno == ESRCH;
	free(stopped.items);
	return signalled;
}
