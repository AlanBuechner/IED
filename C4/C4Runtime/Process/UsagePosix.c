// What a process uses, for Bedrock's Process::GetUsage and Child::GetUsage (ProcessRuntimeC.h) on Linux: processor
// time from its CPU clock, memory from /proc/<id>/status. Its private bytes are its anonymous memory, in RAM or swapped
// out (RssAnon + VmSwap); Linux keeps no peak of that, so the peak given is its peak resident size (VmHWM), or the
// private bytes when they are more. By id it answers only while the process runs (a zombie has ended); a child that
// has ended, not yet reaped, answers with the kernel's totals for it (waitid's rusage): processor time, and the peak
// resident size as both peaks.

#include "ProcessPosix.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { kNsPerSecond = 1000000000, kNsPerMicrosecond = 1000, kBytesPerKiB = 1024, kLineBytes = 256, kStatBytes = 1024 };

// The process's state from /proc/<id>/stat ('Z' a zombie, 'X' dead), or 0 when there is no such process.
static char State(pid_t id)
{
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/stat", (int)id);
	FILE* file = fopen(path, "re");
	if (!file) return 0;
	char text[kStatBytes];
	const size_t length = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[length] = 0;
	const char* fields = strrchr(text, ')');
	char state = 0;
	return fields && sscanf(fields + 1, " %c", &state) == 1 ? state : 0;
}

static bool Running(pid_t id)
{
	const char state = State(id);
	return state != 0 && state != 'Z' && state != 'X';
}

// The processor time of every thread so far; -1 when the system will not say.
static int64_t CpuTimeNs(pid_t id)
{
	clockid_t clock;
	struct timespec time;
	if (clock_getcpuclockid(id, &clock) != 0 || clock_gettime(clock, &time) != 0) return -1;
	return (int64_t)time.tv_sec * kNsPerSecond + time.tv_nsec;
}

// `line`'s number into `*kib` when it is the field `name` ("VmRSS:\t    1234 kB").
static void Field(const char* line, const char* name, int64_t* kib)
{
	const size_t length = strlen(name);
	if (strncmp(line, name, length) == 0) *kib = strtoll(line + length, NULL, 10);
}

static bool ReadMemory(pid_t id, c4_process_counters* usage)
{
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/status", (int)id);
	FILE* file = fopen(path, "re");
	if (!file) return false;
	int64_t resident = 0;
	int64_t peakResident = 0;
	int64_t anonymous = 0;
	int64_t swapped = 0;
	char line[kLineBytes];
	while (fgets(line, sizeof(line), file))
	{
		Field(line, "VmRSS:", &resident);
		Field(line, "VmHWM:", &peakResident);
		Field(line, "RssAnon:", &anonymous);
		Field(line, "VmSwap:", &swapped);
	}
	fclose(file);
	usage->workingSetBytes = resident * kBytesPerKiB;
	usage->peakWorkingSetBytes = peakResident * kBytesPerKiB;
	usage->privateBytes = (anonymous + swapped) * kBytesPerKiB;
	usage->peakPrivateBytes = usage->peakWorkingSetBytes > usage->privateBytes ? usage->peakWorkingSetBytes : usage->privateBytes;
	return true;
}

static int32_t Measure(pid_t id, c4_process_counters* usage)
{
	usage->cpuTimeNs = CpuTimeNs(id);
	if (usage->cpuTimeNs >= 0 && ReadMemory(id, usage)) return 0;
	memset(usage, 0, sizeof(*usage));
	return -1;
}

static int64_t TimevalNs(struct timeval time) { return (int64_t)time.tv_sec * kNsPerSecond + (int64_t)time.tv_usec * kNsPerMicrosecond; }

static int32_t Totals(const struct rusage* totals, c4_process_counters* usage)
{
	usage->cpuTimeNs = TimevalNs(totals->ru_utime) + TimevalNs(totals->ru_stime);
	usage->peakWorkingSetBytes = (int64_t)totals->ru_maxrss * kBytesPerKiB;
	usage->peakPrivateBytes = usage->peakWorkingSetBytes;
	return 0;
}

int32_t c4_process_handle_usage(void* process, c4_process_counters* usage)
{
	if (!usage) return -1;
	memset(usage, 0, sizeof(*usage));
	const c4_process_posix_child* child = (const c4_process_posix_child*)process;
	if (!child) return -1;
	struct rusage totals;
	memset(&totals, 0, sizeof(totals));
	if (c4_process_posix_ended(child, NULL, &totals)) return Totals(&totals, usage);
	return errno == 0 ? Measure(child->id, usage) : -1;
}

int32_t c4_process_usage(uint32_t id, c4_process_counters* usage)
{
	if (!usage) return -1;
	memset(usage, 0, sizeof(*usage));
	if (id == 0 || id > INT_MAX || !Running((pid_t)id)) return -1;
	return Measure((pid_t)id, usage);
}
