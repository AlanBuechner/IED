// Threads on Linux (ThreadRuntimeC.h; ThreadRuntimeC.c has what every system shares): pthreads, and futexes for
// waiting on an address. A futex compares one aligned 32-bit word, so a wait on exactly such a word parks on it; any
// other wait (1, 2 or 8 bytes) parks on a sequence word of the address's bucket, which every wake bumps, so no wake is
// missed between the waiter's look at its bytes and its sleep.

#include "ThreadRuntimeC.h"
#include "ThreadStart.h"
#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

enum
{
	kNanosecondsPerSecond = 1000000000,
	kBuckets = 256,
	kMostProcessors = CPU_SETSIZE,
	kNameRoom = 16, // what the kernel keeps of a thread's name, the terminator included
	kNameMost = 256, // the longest name a thread keeps whole, the terminator included
};

typedef struct C4Thread
{
	pthread_t thread;
} C4Thread;

static void* thread_main(void* packet)
{
	c4_thread_run_packet(packet);
	return NULL;
}

// A thread running entry(state), or NULL when the system starts none. `state` stays alive until the thread runs.
void* c4_thread_start(void* entry, void* state)
{
	C4Thread* thread = (C4Thread*)malloc(sizeof(C4Thread));
	void* packet = thread ? c4_thread_packet(entry, state) : NULL;
	if (!packet)
	{
		free(thread);
		return NULL;
	}
	if (pthread_create(&thread->thread, NULL, thread_main, packet) != 0)
	{
		c4_thread_drop_packet(packet);
		free(thread);
		return NULL;
	}
	return thread;
}

// Waits for the thread to end; the handle is gone afterwards.
void c4_thread_join(void* thread)
{
	if (!thread) return;
	C4Thread* t = (C4Thread*)thread;
	pthread_join(t->thread, NULL);
	free(t);
}

// Lets the thread run on alone; the handle is gone afterwards.
void c4_thread_detach(void* thread)
{
	if (!thread) return;
	C4Thread* t = (C4Thread*)thread;
	pthread_detach(t->thread);
	free(t);
}

uint64_t c4_thread_current_id(void) { return (uint64_t)syscall(SYS_gettid); }

// --- processors -------------------------------------------------------------------------------------------------

static int ProcessProcessors(cpu_set_t* set)
{
	CPU_ZERO(set);
	return sched_getaffinity(0, sizeof(*set), set) == 0;
}

// The processors this process may run on: a pool sized by the system's puts more threads than processors on a process kept to fewer (BUG-454).
uint32_t c4_thread_hardware_count(void)
{
	cpu_set_t set;
	if (ProcessProcessors(&set) && CPU_COUNT(&set) > 0) return (uint32_t)CPU_COUNT(&set);
	const long online = sysconf(_SC_NPROCESSORS_ONLN);
	return online > 0 ? (uint32_t)online : 1u;
}

// The core of logical processor `cpu` as the kernel numbers it (package and core), or -1 when sysfs does not say.
static int64_t CoreOf(int cpu)
{
	char path[96];
	int64_t ids[2] = { 0, 0 };
	const char* names[2] = { "physical_package_id", "core_id" };
	for (int i = 0; i < 2; ++i)
	{
		snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/%s", cpu, names[i]);
		FILE* file = fopen(path, "r");
		if (!file) return -1;
		long long value = 0;
		const int read = fscanf(file, "%lld", &value);
		fclose(file);
		if (read != 1) return -1;
		ids[i] = value;
	}
	return (ids[0] << 32) | (ids[1] & 0xffffffff);
}

// Each core's first logical processor this process may run on, in the system's order, and the core's number (the same for
// every process: it names the core to the others; a processor's own when sysfs says nothing); how many cores. A system whose
// sysfs says nothing about cores counts each logical processor as a core.
static int32_t UsableCores(int firstOfCore[kMostProcessors], int64_t names[kMostProcessors])
{
	cpu_set_t set;
	if (!ProcessProcessors(&set)) return 0;
	int64_t seen[kMostProcessors];
	int32_t found = 0;
	for (int cpu = 0; cpu < kMostProcessors; ++cpu)
	{
		if (!CPU_ISSET(cpu, &set)) continue;
		const int64_t core = CoreOf(cpu);
		int known = 0;
		for (int32_t i = 0; i < found && core >= 0; ++i) known |= seen[i] == core;
		if (known) continue;
		seen[found] = core;
		names[found] = core >= 0 ? core : -1 - (int64_t)cpu;
		firstOfCore[found] = cpu;
		found += 1;
	}
	return found;
}

uint32_t c4_thread_core_count(void)
{
	int cores[kMostProcessors];
	int64_t names[kMostProcessors];
	const int32_t found = UsableCores(cores, names);
	return found > 0 ? (uint32_t)found : c4_thread_hardware_count();
}

// The calling thread on one logical processor of core number `core` of those this process may run on, so that no two threads pinned to
// different cores share one through its hyperthreads. -1 when there is no such core.
int32_t c4_thread_pin_to_core(int32_t core)
{
	if (core < 0) return -1;
	int cores[kMostProcessors];
	int64_t names[kMostProcessors];
	if (core >= UsableCores(cores, names)) return -1;
	cpu_set_t one;
	CPU_ZERO(&one);
	CPU_SET(cores[core], &one);
	return pthread_setaffinity_np(pthread_self(), sizeof(one), &one) == 0 ? 0 : -1;
}

// A claim on a core is a socket bound to a name of the abstract namespace: the name is taken while this process holds the socket,
// and the kernel closes it when the process ends, however it ends. Nothing is written to the file system.
static struct { int64_t name; int fd; int held; } s_coreClaims[kMostProcessors];
static pthread_mutex_t s_coreClaimsLock = PTHREAD_MUTEX_INITIALIZER;

// The core number `core` of those this process may run on, by the name every process knows it by; 0 when there is none.
static int CoreName(int32_t core, int64_t* name)
{
	if (core < 0) return 0;
	int cores[kMostProcessors];
	int64_t names[kMostProcessors];
	if (core >= UsableCores(cores, names)) return 0;
	*name = names[core];
	return 1;
}

static int BindClaim(int64_t name)
{
	const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0) return -1;
	struct sockaddr_un address;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	const int length = snprintf(address.sun_path + 1, sizeof(address.sun_path) - 1, "c4-core-%lld", (long long)name);
	if (bind(fd, (struct sockaddr*)&address, (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + length)) == 0) return fd;
	close(fd);
	return -1;
}

int32_t c4_thread_claim_core(int32_t core)
{
	int64_t name = 0;
	if (!CoreName(core, &name)) return -1;
	int32_t result = -1;
	pthread_mutex_lock(&s_coreClaimsLock);
	int slot = -1;
	int held = 0;
	for (int i = 0; i < kMostProcessors; ++i)
	{
		if (s_coreClaims[i].held && s_coreClaims[i].name == name) held = 1;
		if (!s_coreClaims[i].held && slot < 0) slot = i;
	}
	const int fd = held || slot < 0 ? -1 : BindClaim(name);
	if (fd >= 0)
	{
		s_coreClaims[slot].name = name;
		s_coreClaims[slot].fd = fd;
		s_coreClaims[slot].held = 1;
		result = 0;
	}
	pthread_mutex_unlock(&s_coreClaimsLock);
	return result;
}

void c4_thread_release_core(int32_t core)
{
	int64_t name = 0;
	if (!CoreName(core, &name)) return;
	pthread_mutex_lock(&s_coreClaimsLock);
	for (int i = 0; i < kMostProcessors; ++i)
	{
		if (!s_coreClaims[i].held || s_coreClaims[i].name != name) continue;
		close(s_coreClaims[i].fd);
		s_coreClaims[i].held = 0;
	}
	pthread_mutex_unlock(&s_coreClaimsLock);
}

void c4_thread_yield(void) { sched_yield(); }

void c4_cpu_relax(void) { __builtin_ia32_pause(); }

// The monotonic clock's own sleep, which the kernel ends within microseconds of its time.
void c4_thread_sleep_ns(int64_t ns)
{
	if (ns <= 0)
	{
		sched_yield();
		return;
	}
	struct timespec wait = { (time_t)(ns / kNanosecondsPerSecond), (long)(ns % kNanosecondsPerSecond) };
	while (clock_nanosleep(CLOCK_MONOTONIC, 0, &wait, &wait) == EINTR) {}
}

int64_t c4_thread_sleep_fallbacks(void) { return 0; }

// --- names ------------------------------------------------------------------------------------------------------

// A thread's name as it was given, which c4_thread_name answers (a thread nobody named has none, as on Windows; the
// kernel's own name of a new thread is the program's). Read from the fault handler too: a plain thread-local copy.
static _Thread_local char t_name[kNameMost];

// The kernel keeps 15 bytes of a thread's name, which debuggers and `ps` show: a longer one is cut there, at a
// character's start. The whole name stays the thread's own.
int32_t c4_thread_set_name(const char* name)
{
	if (!name) return -1;
	const size_t given = strlen(name);
	if (given >= kNameMost) return -1;
	memcpy(t_name, name, given + 1);
	char kept[kNameRoom];
	size_t length = given;
	if (length >= kNameRoom)
	{
		length = kNameRoom - 1;
		while (length > 0 && ((unsigned char)name[length] & 0xC0) == 0x80) --length;
	}
	memcpy(kept, name, length);
	kept[length] = 0;
	return pthread_setname_np(pthread_self(), kept) == 0 ? 0 : -1;
}

int32_t c4_thread_name(char* out, int32_t cap)
{
	if (!out || cap <= 0) return -1;
	const size_t length = strlen(t_name);
	if (length >= (size_t)cap) return -1;
	memcpy(out, t_name, length + 1);
	return (int32_t)length;
}

// --- waiting on an address --------------------------------------------------------------------------------------

typedef struct Bucket
{
	volatile int32_t sequence; // bumped by every wake of an address in the bucket
	volatile int32_t waiters;  // threads parked on `sequence`: a wake with none makes no system call
	char padding[56];          // a cache line each
} Bucket;

static Bucket g_buckets[kBuckets];

static Bucket* BucketOf(const void* address)
{
	const uintptr_t key = (uintptr_t)address;
	return &g_buckets[(key >> 3 ^ key >> 11) % kBuckets];
}

static int IsWord(const void* address, uint64_t size) { return size == 4 && ((uintptr_t)address & 3) == 0; }

static long Futex(volatile int32_t* word, int op, int32_t value, const struct timespec* timeout)
{
	return syscall(SYS_futex, (int32_t*)word, op, value, timeout, NULL, 0);
}

static int Equal(const void* address, const void* expected, uint64_t size)
{
	switch (size)
	{
	case 1: return __atomic_load_n((const uint8_t*)address, __ATOMIC_SEQ_CST) == *(const uint8_t*)expected;
	case 2: return __atomic_load_n((const uint16_t*)address, __ATOMIC_SEQ_CST) == *(const uint16_t*)expected;
	case 4: return __atomic_load_n((const uint32_t*)address, __ATOMIC_SEQ_CST) == *(const uint32_t*)expected;
	default: return __atomic_load_n((const uint64_t*)address, __ATOMIC_SEQ_CST) == *(const uint64_t*)expected;
	}
}

// Blocks while the `size` bytes at `address` equal those at `expected`, until a wake or `timeoutNs` (negative:
// none); false when the timeout passed. It may also return early, so callers check their condition again.
bool c4_wait_on_address(void* address, void* expected, uint64_t size, int64_t timeoutNs)
{
	struct timespec timeout = { 0, 0 };
	const struct timespec* limit = NULL;
	if (timeoutNs >= 0)
	{
		timeout.tv_sec = (time_t)(timeoutNs / kNanosecondsPerSecond);
		timeout.tv_nsec = (long)(timeoutNs % kNanosecondsPerSecond);
		limit = &timeout;
	}
	long result = 0;
	if (IsWord(address, size))
		result = Futex((volatile int32_t*)address, FUTEX_WAIT_PRIVATE, *(const int32_t*)expected, limit);
	else
	{
		Bucket* bucket = BucketOf(address);
		__atomic_add_fetch(&bucket->waiters, 1, __ATOMIC_SEQ_CST);
		const int32_t sequence = __atomic_load_n(&bucket->sequence, __ATOMIC_SEQ_CST);
		if (Equal(address, expected, size)) result = Futex(&bucket->sequence, FUTEX_WAIT_PRIVATE, sequence, limit);
		__atomic_sub_fetch(&bucket->waiters, 1, __ATOMIC_SEQ_CST);
	}
	return result == 0 || errno != ETIMEDOUT;
}

static void Wake(void* address, int32_t count)
{
	if (((uintptr_t)address & 3) == 0) Futex((volatile int32_t*)address, FUTEX_WAKE_PRIVATE, count, NULL);
	Bucket* bucket = BucketOf(address);
	__atomic_add_fetch(&bucket->sequence, 1, __ATOMIC_SEQ_CST);
	// The bucket may hold other addresses' waiters: wake them all, and each looks at its own bytes again.
	if (__atomic_load_n(&bucket->waiters, __ATOMIC_SEQ_CST) > 0) Futex(&bucket->sequence, FUTEX_WAKE_PRIVATE, INT32_MAX, NULL);
}

void c4_wake_one(void* address) { Wake(address, 1); }
void c4_wake_all(void* address) { Wake(address, INT32_MAX); }
