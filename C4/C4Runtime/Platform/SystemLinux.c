// System and process information for Bedrock's Platform namespace (PlatformRuntimeC.h) on Linux: processors and caches
// from sysfs, memory from /proc/meminfo and /proc/self/status, the executable from /proc/self/exe, environment
// variables, and getrandom.

#include "PlatformRuntimeC.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <unistd.h>

enum { kDefaultCacheLineBytes = 64, kSysfsTextBytes = 256, kProcTextBytes = 16384, kMostCacheIndexes = 16 };
enum { kBytesPerKibibyte = 1024 };
// readlink says nothing of a path's length; a path is at most this long.
enum { kFirstPathBytes = 4096, kMaxPathBytes = 65536 };
// The user address space of x64 Linux (47-bit, the kernel's TASK_SIZE): what a process can map unless it asks for
// addresses above it on a 5-level-paging machine.
static const int64_t kUserAddressSpaceBytes = 0x7FFFFFFFF000LL;

// --- small readers -----------------------------------------------------------------------------------------

// The whole of a small file (a /proc or /sys one) into `out`, terminated: its length, or -1.
static int64_t ReadSmallFile(const char* path, char* out, size_t capacity)
{
	const int file = open(path, O_RDONLY | O_CLOEXEC);
	if (file < 0) return -1;
	size_t length = 0;
	while (length + 1 < capacity)
	{
		const ssize_t got = read(file, out + length, capacity - 1 - length);
		if (got < 0 && errno == EINTR) continue;
		if (got <= 0) break;
		length += (size_t)got;
	}
	close(file);
	out[length] = 0;
	return (int64_t)length;
}

// The first number of a sysfs list ("2-3,7"), or of a value ("48K"), with its end in `*end`; -1 when there is none.
static int64_t LeadingNumber(const char* text, const char** end)
{
	char* stop = NULL;
	errno = 0;
	const long long value = strtoll(text, &stop, 10);
	if (end) *end = stop;
	return stop == text || errno != 0 ? -1 : (int64_t)value;
}

// A number read from sysfs, -1 when the file is not there.
static int64_t SysfsNumber(const char* path)
{
	char text[kSysfsTextBytes];
	return ReadSmallFile(path, text, sizeof(text)) > 0 ? LeadingNumber(text, NULL) : -1;
}

// A cache size as sysfs writes it ("48K", "1M"): bytes, or -1.
static int64_t SysfsBytes(const char* path)
{
	char text[kSysfsTextBytes];
	if (ReadSmallFile(path, text, sizeof(text)) <= 0) return -1;
	const char* unit = NULL;
	const int64_t value = LeadingNumber(text, &unit);
	if (value < 0) return -1;
	if (*unit == 'K') return value * kBytesPerKibibyte;
	if (*unit == 'M') return value * kBytesPerKibibyte * kBytesPerKibibyte;
	if (*unit == 'G') return value * kBytesPerKibibyte * kBytesPerKibibyte * kBytesPerKibibyte;
	return value;
}

// The value of a "Name:   1234 kB" line of /proc/meminfo or /proc/self/status, in bytes; -1 when there is none.
static int64_t ProcKibField(const char* text, const char* name)
{
	const size_t nameLength = strlen(name);
	for (const char* line = text; *line;)
	{
		if (strncmp(line, name, nameLength) == 0 && line[nameLength] == ':')
		{
			const int64_t kib = LeadingNumber(line + nameLength + 1, NULL);
			return kib < 0 ? -1 : kib * kBytesPerKibibyte;
		}
		const char* end = strchr(line, '\n');
		if (!end) break;
		line = end + 1;
	}
	return -1;
}

// --- processors --------------------------------------------------------------------------------------------

// A logical processor is the first of its core (or package) when its core's list of processors starts with it: so
// each core and package is counted once. False for a processor that is offline (sysfs has no topology for it).
static bool FirstOfList(int32_t processor, const char* list)
{
	char path[kSysfsTextBytes];
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/%s", (int)processor, list);
	return SysfsNumber(path) == processor;
}

static void CountTopology(c4_processor_info* info)
{
	const long configured = sysconf(_SC_NPROCESSORS_CONF);
	for (int32_t processor = 0; processor < (int32_t)configured; ++processor)
	{
		if (FirstOfList(processor, "thread_siblings_list")) info->cores++;
		if (FirstOfList(processor, "core_siblings_list")) info->packages++;
	}
}

static void CountCache(int32_t index, c4_processor_info* info)
{
	char path[kSysfsTextBytes];
	char type[kSysfsTextBytes];
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/type", (int)index);
	if (ReadSmallFile(path, type, sizeof(type)) <= 0) return;
	const bool holdsData = strncmp(type, "Data", 4) == 0 || strncmp(type, "Unified", 7) == 0;
	if (!holdsData) return;
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/level", (int)index);
	const int64_t level = SysfsNumber(path);
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/size", (int)index);
	const int64_t size = SysfsBytes(path);
	if (size <= 0) return;
	if (level == 1)
	{
		info->l1DataBytes = size;
		snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/coherency_line_size", (int)index);
		const int64_t line = SysfsNumber(path);
		if (line > 0) info->cacheLineBytes = (int32_t)line;
	}
	else if (level == 2) info->l2Bytes = size;
	else if (level == 3) info->l3Bytes = size;
}

// What sysfs does not say (a container or VM that hides it), from the C library's reading of CPUID.
static void CountCachesFromCpuid(c4_processor_info* info)
{
	const long l1 = sysconf(_SC_LEVEL1_DCACHE_SIZE);
	const long line = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
	const long l2 = sysconf(_SC_LEVEL2_CACHE_SIZE);
	const long l3 = sysconf(_SC_LEVEL3_CACHE_SIZE);
	if (info->l1DataBytes == 0 && l1 > 0) info->l1DataBytes = l1;
	if (info->cacheLineBytes == 0 && line > 0) info->cacheLineBytes = (int32_t)line;
	if (info->l2Bytes == 0 && l2 > 0) info->l2Bytes = l2;
	if (info->l3Bytes == 0 && l3 > 0) info->l3Bytes = l3;
}

void c4_system_processor_info(c4_processor_info* info)
{
	if (!info) return;
	memset(info, 0, sizeof(*info));
	CountTopology(info);
	for (int32_t index = 0; index < kMostCacheIndexes; ++index) CountCache(index, info);
	CountCachesFromCpuid(info);
	const long logical = sysconf(_SC_NPROCESSORS_ONLN);
	info->logicalProcessors = logical > 0 ? (int32_t)logical : 1;
	if (info->cores == 0 || info->cores > info->logicalProcessors) info->cores = info->logicalProcessors;
	if (info->packages == 0 || info->packages > info->cores) info->packages = 1;
	if (info->cacheLineBytes == 0) info->cacheLineBytes = kDefaultCacheLineBytes;
}

// --- memory ------------------------------------------------------------------------------------------------

// This process's user address space: the system's, or less when RLIMIT_AS limits it.
static int64_t UserAddressSpace(void)
{
	struct rlimit limit;
	if (getrlimit(RLIMIT_AS, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY && (int64_t)limit.rlim_cur < kUserAddressSpaceBytes)
		return (int64_t)limit.rlim_cur;
	return kUserAddressSpaceBytes;
}

static int64_t AtLeastZero(int64_t value) { return value > 0 ? value : 0; }

// Linux overcommits: what Windows calls the commit limit is taken as it is defined, physical memory plus swap, and
// what is left of it as what the system says it could still give (memory available without swapping, plus free swap).
void c4_system_memory_status(c4_memory_status* status)
{
	if (!status) return;
	memset(status, 0, sizeof(*status));
	char text[kProcTextBytes];
	if (ReadSmallFile("/proc/meminfo", text, sizeof(text)) > 0)
	{
		const int64_t total = AtLeastZero(ProcKibField(text, "MemTotal"));
		int64_t available = ProcKibField(text, "MemAvailable");
		if (available < 0) available = AtLeastZero(ProcKibField(text, "MemFree"));
		const int64_t swapTotal = AtLeastZero(ProcKibField(text, "SwapTotal"));
		const int64_t swapFree = AtLeastZero(ProcKibField(text, "SwapFree"));
		status->totalPhysicalBytes = total;
		status->availablePhysicalBytes = available < total ? available : total;
		status->commitLimitBytes = total + swapTotal;
		const int64_t commitLeft = status->availablePhysicalBytes + swapFree;
		status->availableCommitBytes = commitLeft < status->commitLimitBytes ? commitLeft : status->commitLimitBytes;
	}
	status->totalVirtualBytes = UserAddressSpace();
	if (ReadSmallFile("/proc/self/status", text, sizeof(text)) > 0)
		status->availableVirtualBytes = AtLeastZero(status->totalVirtualBytes - AtLeastZero(ProcKibField(text, "VmSize")));
}

// The most private memory this process has been seen to hold: Linux keeps no peak of it, so the peak is the largest
// amount c4_system_process_memory has read.
static _Atomic int64_t g_peakPrivateBytes;

static int64_t RaisePeak(_Atomic int64_t* peak, int64_t value)
{
	int64_t seen = atomic_load(peak);
	while (value > seen && !atomic_compare_exchange_weak(peak, &seen, value)) {}
	return value > seen ? value : seen;
}

// The working set is the resident memory (VmRSS, its peak VmHWM). Private memory is Windows' commit charge, the
// writable private mappings (VmData: heaps, thread stacks, committed reservations, whether touched or not) and the main
// thread's stack (VmStk).
void c4_system_process_memory(c4_process_memory* memory)
{
	if (!memory) return;
	memset(memory, 0, sizeof(*memory));
	char text[kProcTextBytes];
	if (ReadSmallFile("/proc/self/status", text, sizeof(text)) <= 0) return;
	memory->workingSetBytes = AtLeastZero(ProcKibField(text, "VmRSS"));
	memory->peakWorkingSetBytes = AtLeastZero(ProcKibField(text, "VmHWM"));
	if (memory->peakWorkingSetBytes < memory->workingSetBytes) memory->peakWorkingSetBytes = memory->workingSetBytes;
	memory->privateBytes = AtLeastZero(ProcKibField(text, "VmData")) + AtLeastZero(ProcKibField(text, "VmStk"));
	memory->peakPrivateBytes = RaisePeak(&g_peakPrivateBytes, memory->privateBytes);
}

uint64_t c4_system_process_id(void) { return (uint64_t)getpid(); }

static size_t NextPathBytes(size_t bytes) { return bytes * 2 < kMaxPathBytes ? bytes * 2 : kMaxPathBytes; }

// The file /proc/self/exe links to; " (deleted)" follows it when the program's file was deleted or replaced since.
int64_t c4_system_executable_path(char* out, int64_t capacity)
{
	for (size_t bytes = kFirstPathBytes;; bytes = NextPathBytes(bytes))
	{
		char* path = (char*)malloc(bytes);
		if (!path) return -1;
		const ssize_t length = readlink("/proc/self/exe", path, bytes);
		const bool whole = length > 0 && (size_t)length < bytes;
		if (whole && out && length < capacity)
		{
			memcpy(out, path, (size_t)length);
			out[length] = 0;
		}
		free(path);
		if (whole) return (int64_t)length;
		if (length <= 0 || bytes == kMaxPathBytes) return -1;
	}
}

// --- environment -------------------------------------------------------------------------------------------

// getenv, setenv and unsetenv may not run at once (setenv moves the table getenv reads): the runtime's own calls go one
// at a time. A value is copied out before the lock is let go.
static pthread_mutex_t g_environment = PTHREAD_MUTEX_INITIALIZER;

// The length of the UTF-8 character starting with `lead` (0 for a byte that cannot start one), and the smallest code
// point that length may encode (a smaller one is an overlong form).
static int32_t SequenceLength(unsigned char lead, uint32_t* smallest)
{
	if (lead < 0x80) { *smallest = 0; return 1; }
	if ((lead & 0xE0) == 0xC0) { *smallest = 0x80; return 2; }
	if ((lead & 0xF0) == 0xE0) { *smallest = 0x800; return 3; }
	if ((lead & 0xF8) == 0xF0) { *smallest = 0x10000; return 4; }
	return 0;
}

// Text Windows' strict conversion accepts: well-formed UTF-8, no overlong forms, surrogates or code points past
// U+10FFFF, so a name or value Windows refuses is refused here too.
static bool IsUtf8(const char* text)
{
	for (const unsigned char* next = (const unsigned char*)text; *next;)
	{
		uint32_t smallest = 0;
		const int32_t length = SequenceLength(*next, &smallest);
		if (length == 0) return false;
		uint32_t point = length == 1 ? *next : (uint32_t)(*next & (0x7F >> length));
		for (int32_t i = 1; i < length; ++i)
		{
			if ((next[i] & 0xC0) != 0x80) return false;
			point = (point << 6) | (next[i] & 0x3F);
		}
		if (point < smallest || point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF)) return false;
		next += length;
	}
	return true;
}

static bool IsVariableName(const char* name) { return name && *name && !strchr(name, '=') && IsUtf8(name); }

int64_t c4_system_get_environment(const char* name, char* out, int64_t capacity)
{
	if (!IsVariableName(name)) return -1;
	pthread_mutex_lock(&g_environment);
	const char* value = getenv(name);
	const int64_t length = value ? (int64_t)strlen(value) : -1;
	if (value && out && length < capacity) memcpy(out, value, (size_t)length + 1);
	pthread_mutex_unlock(&g_environment);
	return length;
}

int32_t c4_system_set_environment(const char* name, const char* value)
{
	if (!IsVariableName(name) || (value && !IsUtf8(value))) return -1;
	pthread_mutex_lock(&g_environment);
	const int result = value ? setenv(name, value, 1) : unsetenv(name);
	pthread_mutex_unlock(&g_environment);
	return result == 0 ? 0 : -1;
}

// --- random bytes ------------------------------------------------------------------------------------------

// getrandom gives at most this many bytes a call (the kernel caps one read at 32 MiB - 1).
enum { kRandomChunkBytes = 1 << 24 };

// What getrandom gives, `*unavailable` when the kernel has no getrandom (older than 3.17) or a sandbox forbids it.
static bool FillFromGetrandom(unsigned char* next, int64_t count, bool* unavailable)
{
	while (count > 0)
	{
		const size_t chunk = count > kRandomChunkBytes ? (size_t)kRandomChunkBytes : (size_t)count;
		const ssize_t got = getrandom(next, chunk, 0);
		if (got < 0 && errno == EINTR) continue;
		if (got <= 0)
		{
			*unavailable = got < 0 && (errno == ENOSYS || errno == EPERM);
			return false;
		}
		next += got;
		count -= got;
	}
	return true;
}

static bool FillFromDevice(unsigned char* next, int64_t count)
{
	const int file = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (file < 0) return false;
	while (count > 0)
	{
		const size_t chunk = count > kRandomChunkBytes ? (size_t)kRandomChunkBytes : (size_t)count;
		const ssize_t got = read(file, next, chunk);
		if (got < 0 && errno == EINTR) continue;
		if (got <= 0) break;
		next += got;
		count -= got;
	}
	close(file);
	return count == 0;
}

int32_t c4_system_random_bytes(void* out, int64_t count)
{
	if (count < 0 || (count > 0 && !out)) return -1;
	bool unavailable = false;
	if (FillFromGetrandom((unsigned char*)out, count, &unavailable)) return 0;
	return unavailable && FillFromDevice((unsigned char*)out, count) ? 0 : -1;
}
