// The collector's operating-system services on Linux (GCPlatform.h; GCPlatformWin32.c is Windows'). Threads stop by a
// signal, as the Boehm collector's do: SIGPWR makes a thread save its registers on its own stack, say it stopped and
// wait; SIGXCPU lets it go on. Linux keeps no record of written pages a process can read and reset per range, so
// written-page tracking is not available (gc_os_tracks_writes): the collector does not mark concurrently here.

#include "GC/GCPlatform.h"
#include "GC/GCMemoryPosix.h"
#include "PlatformRuntimeC.h"
#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>

enum
{
	kSuspendSignal = SIGPWR,
	kResumeSignal = SIGXCPU,
	kCaptureMost = 64,
};

int gc_os_tracks_writes(void) { return 0; }

void gc_os_forget_writes(void* base, size_t size)
{
	(void)base; (void)size;
}

// Never called while gc_os_tracks_writes says no (GCConcurrent); every page is reported, which is always correct.
void gc_os_take_writes(void* base, size_t size, gc_page_fn written, void* ctx)
{
	written(ctx, (uint8_t*)base, size);
}

void* gc_os_map(size_t size, size_t align)
{
	// Map enough to hold an aligned range, then give back what lies before and after it.
	const size_t span = size + align;
	uint8_t* probe = (uint8_t*)mmap(NULL, span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (probe == MAP_FAILED) return NULL;
	uint8_t* aligned = (uint8_t*)(((uintptr_t)probe + align - 1) & ~(uintptr_t)(align - 1));
	if (aligned > probe) munmap(probe, (size_t)(aligned - probe));
	uint8_t* end = aligned + size;
	if (end < probe + span) munmap(end, (size_t)(probe + span - end));
	return aligned;
}

// The pages go back to the system at once and read as zeroes when next touched.
void gc_os_decommit(void* start, size_t size)
{
	madvise(start, size, MADV_DONTNEED);
}

int gc_os_commit(void* start, size_t size)
{
	(void)start; (void)size;
	return 1;
}

uint64_t gc_os_now_ns(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// ------------------------------------------------------------------------------------------------
// The collector's memory and lock

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static pthread_mutex_t g_scratchLock = PTHREAD_MUTEX_INITIALIZER;
static GCPosixHeap g_heap;
static GCPosixHeap g_scratch;
static pthread_key_t g_exitKey;

static void start_platform(void);

static void platform(void)
{
	pthread_once(&g_once, start_platform);
}

void gc_os_lock(void)
{
	platform();
	pthread_mutex_lock(&g_lock);
}

void gc_os_unlock(void)
{
	pthread_mutex_unlock(&g_lock);
}

void* gc_mem_alloc(size_t size)
{
	gc_os_lock();
	void* p = gc_posix_heap_alloc(&g_heap, size);
	gc_os_unlock();
	return p;
}

void* gc_mem_realloc(void* p, size_t size)
{
	gc_os_lock();
	void* grown = gc_posix_heap_realloc(&g_heap, p, size);
	gc_os_unlock();
	return grown;
}

void gc_mem_free(void* p)
{
	if (!p) return;
	gc_os_lock();
	gc_posix_heap_free(&g_heap, p);
	gc_os_unlock();
}

void* gc_os_scratch_realloc(void* p, size_t size)
{
	platform();
	pthread_mutex_lock(&g_scratchLock);
	void* grown = gc_posix_heap_realloc(&g_scratch, p, size);
	pthread_mutex_unlock(&g_scratchLock);
	return grown;
}

void gc_os_scratch_free(void* p)
{
	if (!p) return;
	pthread_mutex_lock(&g_scratchLock);
	gc_posix_heap_free(&g_scratch, p);
	pthread_mutex_unlock(&g_scratchLock);
}

// ------------------------------------------------------------------------------------------------
// Mark helpers

typedef struct HelperStart
{
	void (*run)(void*);
	void* arg;
} HelperStart;

static void* helper_main(void* parameter)
{
	HelperStart start = *(HelperStart*)parameter;
	gc_os_scratch_free(parameter);
	start.run(start.arg);
	return NULL;
}

int gc_os_start_helper(void (*run)(void*), void* arg)
{
	HelperStart* start = (HelperStart*)gc_os_scratch_realloc(NULL, sizeof(HelperStart));
	if (!start) return 0;
	start->run = run;
	start->arg = arg;
	pthread_attr_t attributes;
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	pthread_t thread;
	const int failed = pthread_create(&thread, &attributes, helper_main, start);
	pthread_attr_destroy(&attributes);
	if (failed)
	{
		gc_os_scratch_free(start);
		return 0;
	}
	return 1;
}

// The processors this process may run on (a container's or taskset's share), not the machine's.
uint32_t gc_os_processor_count(void)
{
	cpu_set_t set;
	if (sched_getaffinity(0, sizeof(set), &set) == 0)
	{
		const int count = CPU_COUNT(&set);
		if (count > 0) return (uint32_t)count;
	}
	const long online = sysconf(_SC_NPROCESSORS_ONLN);
	return online > 0 ? (uint32_t)online : 1u;
}

void gc_os_relax(void) { __builtin_ia32_pause(); }

// A futex on the low 32 bits of the long (little-endian): the values parked on stay far below 2^31.
void gc_os_park(volatile long* address, long expected)
{
	syscall(SYS_futex, (int*)address, FUTEX_WAIT_PRIVATE, (int)expected, NULL, NULL, 0);
}

void gc_os_wake_all(volatile long* address)
{
	syscall(SYS_futex, (int*)address, FUTEX_WAKE_PRIVATE, 0x7fffffff, NULL, NULL, 0);
}

// ------------------------------------------------------------------------------------------------
// Threads
//
// A registered thread's record (GCOsThread.handle) lives in the collector's heap. To stop it, gc_os_suspend sends
// SIGPWR and waits on `stopped`; the thread's handler saves its registers, notes where its stack is, posts `stopped`
// and waits in sigsuspend until gc_os_resume sets `resume` and sends SIGXCPU. The stack from the handler's frame up
// holds the interrupted frames and the kernel's copy of every register, red zone included.

typedef struct PosixThread
{
	pthread_t thread;
	sem_t stopped;
	volatile int resume;
	const void* stopSp;       // the handler's stack pointer while it is stopped
	mcontext_t registers;     // the interrupted context's general registers
	const void* stackTop;
} PosixThread;

static __thread PosixThread* t_self;

static void on_suspend(int signal, siginfo_t* info, void* context)
{
	(void)signal; (void)info;
	const int saved = errno;
	PosixThread* self = t_self;
	if (!self)
	{
		errno = saved;
		return;
	}
	self->registers = ((ucontext_t*)context)->uc_mcontext;
	volatile uintptr_t marker = 0;
	self->stopSp = (const void*)&marker;
	sigset_t wait;
	sigfillset(&wait);
	sigdelset(&wait, kResumeSignal);
	sem_post(&self->stopped);
	while (!__atomic_load_n(&self->resume, __ATOMIC_ACQUIRE)) sigsuspend(&wait);
	__atomic_store_n(&self->resume, 0, __ATOMIC_RELAXED);
	errno = saved;
}

static void on_resume(int signal)
{
	(void)signal; // only wakes sigsuspend
}

static void on_thread_exit(void* data);

static void start_platform(void)
{
	struct sigaction suspend;
	memset(&suspend, 0, sizeof(suspend));
	suspend.sa_sigaction = on_suspend;
	suspend.sa_flags = SA_SIGINFO | SA_RESTART;
	sigfillset(&suspend.sa_mask); // nothing interrupts a stopped thread; SIGXCPU waits for its sigsuspend
	struct sigaction resume;
	memset(&resume, 0, sizeof(resume));
	resume.sa_handler = on_resume;
	resume.sa_flags = SA_RESTART;
	sigfillset(&resume.sa_mask);
	if (sigaction(kSuspendSignal, &suspend, NULL) != 0 || sigaction(kResumeSignal, &resume, NULL) != 0 ||
		pthread_key_create(&g_exitKey, on_thread_exit) != 0)
		gc_os_fatal("the collector could not start");
}

static const void* stack_top_of_this_thread(void)
{
	pthread_attr_t attributes;
	void* low = NULL;
	size_t size = 0;
	if (pthread_getattr_np(pthread_self(), &attributes) != 0) gc_os_fatal("the collector could not find a thread's stack");
	pthread_attr_getstack(&attributes, &low, &size);
	pthread_attr_destroy(&attributes);
	return (const uint8_t*)low + size;
}

GCOsThread gc_os_this_thread(void)
{
	platform();
	PosixThread* self = (PosixThread*)gc_mem_alloc(sizeof(PosixThread));
	if (!self || sem_init(&self->stopped, 0, 0) != 0) gc_os_fatal("the collector could not register a thread");
	self->thread = pthread_self();
	self->stackTop = stack_top_of_this_thread();
	t_self = self;
	// A thread that blocks the two signals could never stop.
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, kSuspendSignal);
	sigaddset(&mask, kResumeSignal);
	pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
	GCOsThread thread;
	thread.handle = self;
	thread.id = gc_os_this_thread_id();
	thread.stackTop = self->stackTop;
	return thread;
}

void gc_os_release_thread(GCOsThread* thread)
{
	PosixThread* record = (PosixThread*)thread->handle;
	if (!record) return;
	if (t_self == record) t_self = NULL;
	sem_destroy(&record->stopped);
	gc_mem_free(record);
	thread->handle = NULL;
}

uint64_t gc_os_this_thread_id(void) { return (uint64_t)syscall(SYS_gettid); }

int gc_os_suspend(const GCOsThread* thread)
{
	PosixThread* record = (PosixThread*)thread->handle;
	if (!record || pthread_kill(record->thread, kSuspendSignal) != 0) return 0;
	while (sem_wait(&record->stopped) != 0)
		if (errno != EINTR) return 0;
	return 1;
}

void gc_os_resume(const GCOsThread* thread)
{
	PosixThread* record = (PosixThread*)thread->handle;
	__atomic_store_n(&record->resume, 1, __ATOMIC_RELEASE);
	pthread_kill(record->thread, kResumeSignal);
}

void gc_os_scan_stack(gc_scan_range_fn scan, void* ctx)
{
	// Optimized code keeps pointers in callee-saved registers across the call into the collector: they are saved into
	// this frame's jmp_buf, and the scan starts at the jmp_buf so everything above it is covered too.
	jmp_buf registers;
	setjmp(registers);
	scan(ctx, &registers, (const uint8_t*)&registers + sizeof(registers));
	const void* top = t_self ? t_self->stackTop : stack_top_of_this_thread();
	const uint8_t* from = (const uint8_t*)&registers + sizeof(registers);
	if ((const void*)from < top) scan(ctx, from, top);
}

void gc_os_scan_suspended(const GCOsThread* thread, gc_scan_range_fn scanNow, gc_scan_range_fn scanStack, void* ctx)
{
	PosixThread* record = (PosixThread*)thread->handle;
	mcontext_t registers = record->registers;
	scanNow(ctx, &registers, (const uint8_t*)&registers + sizeof(registers));
	if (record->stopSp < record->stackTop) scanStack(ctx, record->stopSp, record->stackTop);
}

// ------------------------------------------------------------------------------------------------
// Stacks and guarded reads

typedef struct Walk
{
	void** frames;
	uint32_t capacity;
	uint32_t count;
} Walk;

static _Unwind_Reason_Code walk_frame(struct _Unwind_Context* context, void* data)
{
	Walk* walk = (Walk*)data;
	if (walk->count >= walk->capacity) return _URC_END_OF_STACK;
	const uintptr_t ip = _Unwind_GetIP(context);
	if (!ip) return _URC_END_OF_STACK;
	walk->frames[walk->count++] = (void*)ip;
	return _URC_NO_REASON;
}

uint32_t gc_os_capture_stack(void** frames, uint32_t capacity, const void* from)
{
	void* all[kCaptureMost];
	Walk walk = { all, kCaptureMost, 0 };
	_Unwind_Backtrace(walk_frame, &walk);
	uint32_t start = 0;
	while (start < walk.count && all[start] != from) ++start;
	if (start == walk.count) start = walk.count > 1 ? 1 : 0; // from the caller's frame out
	uint32_t taken = 0;
	for (uint32_t i = start; i < walk.count && taken < capacity; ++i) frames[taken++] = all[i];
	return taken;
}

// A fault while a guarded read runs jumps back to it; any other goes to whoever handled the signal before.
static __thread sigjmp_buf* t_guard;
static struct sigaction g_previousSegv;
static struct sigaction g_previousBus;
static pthread_once_t g_guardOnce = PTHREAD_ONCE_INIT;

static void chain(const struct sigaction* previous, int signal, siginfo_t* info, void* context)
{
	if (previous->sa_flags & SA_SIGINFO)
	{
		if (previous->sa_sigaction) previous->sa_sigaction(signal, info, context);
		return;
	}
	if (previous->sa_handler == SIG_IGN) return;
	if (previous->sa_handler != SIG_DFL)
	{
		previous->sa_handler(signal);
		return;
	}
	// The default: die of the signal, as an unhandled fault does.
	sigaction(signal, previous, NULL);
	raise(signal);
}

static void on_fault(int signal, siginfo_t* info, void* context)
{
	if (t_guard) siglongjmp(*t_guard, 1);
	chain(signal == SIGBUS ? &g_previousBus : &g_previousSegv, signal, info, context);
}

static void install_guard(void)
{
	struct sigaction fault;
	memset(&fault, 0, sizeof(fault));
	fault.sa_sigaction = on_fault;
	fault.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
	sigemptyset(&fault.sa_mask);
	sigaction(SIGSEGV, &fault, &g_previousSegv);
	sigaction(SIGBUS, &fault, &g_previousBus);
}

int64_t gc_os_count_guarded(int64_t (*count)(const void* object), const void* object)
{
	pthread_once(&g_guardOnce, install_guard);
	sigjmp_buf guard;
	if (sigsetjmp(guard, 1))
	{
		t_guard = NULL;
		return 0;
	}
	t_guard = &guard;
	const int64_t counted = count(object);
	t_guard = NULL;
	return counted;
}

void gc_os_yield(void) { sched_yield(); }

// ------------------------------------------------------------------------------------------------
// Thread exit

typedef struct ExitCallback
{
	void (*on_exit)(void*);
	void* data;
} ExitCallback;

// Runs when a thread ends by returning or pthread_exit, never when the process exits, so no other thread's locks
// are in question.
static void on_thread_exit(void* data)
{
	ExitCallback* callback = (ExitCallback*)data;
	if (!callback) return;
	callback->on_exit(callback->data);
	gc_mem_free(callback);
}

void gc_os_on_thread_exit(void (*on_exit)(void*), void* data)
{
	platform();
	ExitCallback* callback = (ExitCallback*)gc_mem_alloc(sizeof(ExitCallback));
	callback->on_exit = on_exit;
	callback->data = data;
	pthread_setspecific(g_exitKey, callback);
}

void gc_os_fatal(const char* message)
{
	c4_fatal(message);
}
