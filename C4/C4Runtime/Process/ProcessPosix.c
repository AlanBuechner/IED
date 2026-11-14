// Child processes for Bedrock's Process namespace (ProcessRuntimeC.h) on Linux, started with posix_spawn (glibc's
// clone that shares memory until the exec, safe in a threaded, collected process). Only the child's ends of its pipes
// are inherited: every descriptor made here is close-on-exec, and any other this process holds without that flag is
// closed in the child. A child that shares none of this process's standard streams starts in a session of its own with
// no terminal (on Windows it gets a console nobody sees), so a kill ends its process group and session as well as all
// it finds below it; one that shares a stream stays in this process's group, as on Windows it shares the console, so a
// terminal's Ctrl+C and keyboard reach it too. A terminal is a pseudo-terminal that becomes the child's controlling
// terminal. The child starts with no signal blocked and SIGPIPE at its default even when this process ignores it.
// Closing a child with killOnClose ends what it started; a parent that dies without closing it leaves it running, as
// Linux has no job object (PR_SET_PDEATHSIG would end it with the thread that started it, not the process).

#include "ProcessPosix.h"
#include "SystemErrorC.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

enum
{
	kDefaultColumns = 80,
	kDefaultRows = 25,
	kKilledExitCode = 1,          // Windows' TerminateJobObject code
	kSignalExitBase = 128,        // an ending by signal N is 128 + N, as shells report it
	kSlaveNameBytes = 128,
	kLongestPauseMs = 20,         // between looks at a child, on kernels without pidfd_open
};

static const char* const kNullDevice = "/dev/null";
static const char* const kPseudoTerminals = "/dev/ptmx";

// What posix_spawn needs besides the program, made before it and released after: the child's ends of its pipes (a
// terminal's slave the child opens itself, by name), what it does with its descriptors, and its attributes.
typedef struct Launch
{
	int childEnds[3];
	char slave[kSlaveNameBytes];
	posix_spawn_file_actions_t actions;
	posix_spawnattr_t attributes;
	bool actionsMade;
	bool attributesMade;
} Launch;

static void EndLaunch(Launch* launch)
{
	for (int i = 0; i < 3; ++i)
		if (launch->childEnds[i] >= 0) close(launch->childEnds[i]);
	if (launch->actionsMade) posix_spawn_file_actions_destroy(&launch->actions);
	if (launch->attributesMade) posix_spawnattr_destroy(&launch->attributes);
}

// --- the descriptors -------------------------------------------------------------------------------------------------

// Closes in the child every descriptor (3 and above) this process holds without close-on-exec: a stray one kept open
// in a child would keep a pipe or socket of this process alive. Without /proc they stay open, as with any spawn.
static int CloseStrays(Launch* launch)
{
	DIR* folder = opendir("/proc/self/fd");
	if (!folder) return 0;
	const int listing = dirfd(folder);
	int why = 0;
	for (struct dirent* entry; !why && (entry = readdir(folder)) != NULL;)
	{
		char* end = NULL;
		const long fd = strtol(entry->d_name, &end, 10);
		if (end == entry->d_name || *end || fd < 3 || fd == listing || fd > INT_MAX) continue;
		const int flags = fcntl((int)fd, F_GETFD);
		if (flags >= 0 && !(flags & FD_CLOEXEC)) why = posix_spawn_file_actions_addclose(&launch->actions, (int)fd);
	}
	closedir(folder);
	return why;
}

// A pipe for `stream` (whose number is its descriptor): the parent's end into `out`, the child's dup'ed onto it.
static int PreparePipe(int32_t stream, c4_process* out, Launch* launch)
{
	int parent = -1;
	if (!c4_process_posix_pipe(stream != kProcessStdin, &parent, &launch->childEnds[stream])) return errno;
	out->pipes[stream] = c4_process_posix_handle(parent);
	return posix_spawn_file_actions_adddup2(&launch->actions, launch->childEnds[stream], stream);
}

// The child's end of each standard stream as its mode asks, and the parent's end of each pipe. 0 or the errno.
static int PrepareStreams(const c4_process_options* start, c4_process* out, Launch* launch)
{
	const int32_t modes[3] = { start->stdinMode, start->stdoutMode, start->stderrMode };
	for (int32_t stream = 0; stream < 3; ++stream)
	{
		const int32_t mode = modes[stream];
		if (mode < kProcessInherit || mode > kProcessToOutput || (mode == kProcessToOutput && stream != kProcessStderr)) return EINVAL;
		int why = 0;
		if (mode == kProcessPipe) why = PreparePipe(stream, out, launch);
		if (mode == kProcessNull)
			why = posix_spawn_file_actions_addopen(&launch->actions, stream, kNullDevice, stream == kProcessStdin ? O_RDONLY : O_WRONLY, 0);
		if (mode == kProcessToOutput) why = posix_spawn_file_actions_adddup2(&launch->actions, kProcessStdout, kProcessStderr);
		if (why) return why;
	}
	return 0;
}

// A terminal's size as a pseudo-terminal takes it; false when it is none (as on Windows, at most SHRT_MAX).
static bool TerminalSize(int32_t columns, int32_t rows, struct winsize* size)
{
	if (columns <= 0 || rows <= 0 || columns > SHRT_MAX || rows > SHRT_MAX) return false;
	memset(size, 0, sizeof(*size));
	size->ws_col = (unsigned short)columns;
	size->ws_row = (unsigned short)rows;
	return true;
}

// Another descriptor of `fd`, 3 or above and close-on-exec, into a handle field. 0 or the errno.
static int Duplicate(int fd, void** handle)
{
	const int copy = fcntl(fd, F_DUPFD_CLOEXEC, 3);
	if (copy < 0) return errno;
	*handle = c4_process_posix_handle(copy);
	return 0;
}

// The pseudo-terminal a terminal runs under. The parent writes the keyboard into its master and reads the screen from
// it (stdin's and stdout's handles are each a descriptor of it; the console handle another, for resizing), so closing
// stdin does not end a terminal's input: its end-of-file key (Ctrl+D) does. The child, leading a new session, opens
// the slave as its standard streams and so takes it as its controlling terminal.
static int PrepareTerminal(const c4_process_options* start, c4_process* out, Launch* launch)
{
	const int master = c4_process_posix_raise(open(kPseudoTerminals, O_RDWR | O_NOCTTY | O_CLOEXEC));
	if (master < 0) return errno;
	out->console = c4_process_posix_handle(master);
	struct winsize size;
	if (!TerminalSize(start->columns, start->rows, &size)) TerminalSize(kDefaultColumns, kDefaultRows, &size);
	if (grantpt(master) != 0 || unlockpt(master) != 0) return errno;
	int why = ptsname_r(master, launch->slave, sizeof(launch->slave));
	if (!why && (ioctl(master, TIOCSWINSZ, &size) != 0 || fcntl(master, F_SETFL, O_NONBLOCK) != 0)) why = errno;
	if (!why) why = Duplicate(master, &out->pipes[kProcessStdin]);
	if (!why) why = Duplicate(master, &out->pipes[kProcessStdout]);
	if (!why) why = posix_spawn_file_actions_addopen(&launch->actions, STDIN_FILENO, launch->slave, O_RDWR, 0);
	if (!why) why = posix_spawn_file_actions_adddup2(&launch->actions, STDIN_FILENO, STDOUT_FILENO);
	if (!why) why = posix_spawn_file_actions_adddup2(&launch->actions, STDIN_FILENO, STDERR_FILENO);
	return why;
}

// --- starting --------------------------------------------------------------------------------------------------------

static bool SharesStream(const c4_process_options* start)
{
	return start->stdinMode == kProcessInherit || start->stdoutMode == kProcessInherit || start->stderrMode == kProcessInherit;
}

// The child's session (a new one with `ownSession`) and signals: none blocked, SIGPIPE and SIGXFSZ at their defaults.
static int PrepareAttributes(bool ownSession, Launch* launch)
{
	sigset_t none;
	sigset_t defaults;
	sigemptyset(&none);
	sigemptyset(&defaults);
	sigaddset(&defaults, SIGPIPE);
	sigaddset(&defaults, SIGXFSZ);
	const short flags = (short)(POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | (ownSession ? POSIX_SPAWN_SETSID : 0));
	int why = posix_spawnattr_setsigmask(&launch->attributes, &none);
	if (!why) why = posix_spawnattr_setsigdefault(&launch->attributes, &defaults);
	if (!why) why = posix_spawnattr_setflags(&launch->attributes, flags);
	return why;
}

static int Spawn(const c4_process_options* start, const char* program, c4_process_posix_child* child, Launch* launch)
{
	bool invalidEnvironment = false;
	char** arguments = c4_process_posix_arguments(start);
	char** environment = c4_process_posix_environment(start, &invalidEnvironment);
	int why = invalidEnvironment ? EINVAL : !arguments || !environment ? ENOMEM : 0;
	if (!why) why = posix_spawn(&child->id, program, &launch->actions, &launch->attributes, arguments, environment);
	free(arguments);
	free(environment);
	return why;
}

// 0 or the errno.
static int Start(const c4_process_options* start, const char* program, c4_process* out, c4_process_posix_child* child)
{
	Launch launch;
	memset(&launch, 0, sizeof(launch));
	for (int i = 0; i < 3; ++i) launch.childEnds[i] = -1;
	child->leadsSession = start->terminal || !SharesStream(start);
	child->killOnClose = start->killOnClose != 0;
	int why = posix_spawn_file_actions_init(&launch.actions);
	launch.actionsMade = why == 0;
	if (!why) why = posix_spawnattr_init(&launch.attributes);
	launch.attributesMade = launch.actionsMade && why == 0;
	if (!why) why = CloseStrays(&launch);
	if (!why) why = start->terminal ? PrepareTerminal(start, out, &launch) : PrepareStreams(start, out, &launch);
	if (!why && start->workingDirectory && *start->workingDirectory)
		why = posix_spawn_file_actions_addchdir_np(&launch.actions, start->workingDirectory);
	if (!why) why = PrepareAttributes(child->leadsSession, &launch);
	if (!why) why = Spawn(start, program, child, &launch);
	EndLaunch(&launch);
	return why;
}

// Whether `folder` is named and missing, with `error` saying so: the system would only say a file was not found.
static bool FolderMissing(const char* folder, char* error, int64_t errorCapacity)
{
	struct stat status;
	if (!folder || !*folder || (stat(folder, &status) == 0 && S_ISDIR(status.st_mode))) return false;
	char message[512];
	snprintf(message, sizeof(message), "the working directory \"%s\" is not a folder that exists", folder);
	c4_copy_text(message, error, errorCapacity);
	return true;
}

// A descriptor that turns readable when the child ends (pidfd_open, Linux 5.3); -1 on older kernels.
static int OpenPidfd(pid_t id) { return c4_process_posix_raise((int)syscall(__NR_pidfd_open, id, 0)); }

int32_t c4_process_start(const c4_process_options* start, c4_process* out, char* error, int64_t errorCapacity)
{
	memset(out, 0, sizeof(*out));
	c4_copy_text("", error, errorCapacity);
	const bool shell = start && start->shellCommand && *start->shellCommand;
	if (!start || (!shell && (!start->program || !*start->program)))
	{
		c4_copy_text("no program to start", error, errorCapacity);
		return -1;
	}
	if (shell && ((start->program && *start->program) || start->argumentCount > 0))
	{
		c4_copy_text("a shell command takes no program or arguments", error, errorCapacity);
		return -1;
	}
	if (FolderMissing(start->workingDirectory, error, errorCapacity)) return -1;
	char* program = shell ? c4_process_posix_shell(error, errorCapacity) : c4_process_posix_program(start->program, error, errorCapacity);
	if (!program) return -1;
	c4_process_posix_child* child = (c4_process_posix_child*)calloc(1, sizeof(c4_process_posix_child));
	const int why = child ? Start(start, program, out, child) : ENOMEM;
	free(program);
	if (why == 0)
	{
		child->pidfd = OpenPidfd(child->id);
		out->process = child;
		out->id = (uint32_t)child->id;
		return 0;
	}
	free(child);
	c4_process_close(out);
	c4_system_error_text((uint32_t)why, error, errorCapacity);
	return -1;
}

// --- the child's life ------------------------------------------------------------------------------------------------

bool c4_process_posix_ended(const c4_process_posix_child* child, siginfo_t* info, struct rusage* usage)
{
	siginfo_t status;
	siginfo_t* into = info ? info : &status;
	memset(into, 0, sizeof(*into));
	long asked;
	do asked = syscall(SYS_waitid, P_PID, (id_t)child->id, into, WEXITED | WNOWAIT | WNOHANG, usage);
	while (asked < 0 && errno == EINTR);
	if (asked < 0) return false;
	errno = 0;
	return into->si_pid == child->id;
}

static void Pause(int32_t ms)
{
	const struct timespec pause = { ms / 1000, (long)(ms % 1000) * 1000000 };
	nanosleep(&pause, NULL);
}

// Waits up to `timeoutMs` for the child to end, its status into `info`: 1, 0 when it has not, or -1.
static int32_t WaitEnded(const c4_process_posix_child* child, int32_t timeoutMs, siginfo_t* info)
{
	const int64_t deadline = c4_process_posix_deadline(timeoutMs);
	for (int32_t pauseMs = 1;; pauseMs = pauseMs < kLongestPauseMs ? pauseMs * 2 : kLongestPauseMs)
	{
		if (c4_process_posix_ended(child, info, NULL)) return 1;
		if (errno != 0) return -1;
		const int32_t left = c4_process_posix_remaining(deadline);
		if (left == 0) return 0;
		struct pollfd ended = { child->pidfd, POLLIN, 0 };
		if (child->pidfd >= 0) poll(&ended, 1, left);
		else Pause(left > 0 && left < pauseMs ? left : pauseMs);
	}
}

static int64_t ExitCode(const c4_process_posix_child* child, const siginfo_t* info)
{
	if (info->si_code == CLD_EXITED) return (int64_t)(info->si_status & 0xff);
	if (child->killed && info->si_status == SIGKILL) return kKilledExitCode;
	return kSignalExitBase + info->si_status;
}

int64_t c4_process_wait(c4_process* process, int32_t timeoutMs)
{
	const c4_process_posix_child* child = process ? (const c4_process_posix_child*)process->process : NULL;
	if (!child) return kProcessError;
	siginfo_t info;
	const int32_t waited = WaitEnded(child, timeoutMs, &info);
	if (waited == 0) return kProcessTimedOut;
	return waited < 0 ? kProcessError : ExitCode(child, &info);
}

int32_t c4_process_kill(c4_process* process)
{
	c4_process_posix_child* child = process ? (c4_process_posix_child*)process->process : NULL;
	if (!child) return -1;
	if (!c4_process_posix_ended(child, NULL, NULL)) child->killed = true;
	return c4_process_posix_kill_tree(child->id, child->leadsSession) ? 0 : -1;
}

int32_t c4_process_resize(c4_process* process, int32_t columns, int32_t rows)
{
	const int console = process ? c4_process_posix_fd(process->console) : -1;
	struct winsize size;
	if (console < 0 || !TerminalSize(columns, rows, &size)) return -1;
	return ioctl(console, TIOCSWINSZ, &size) == 0 ? 0 : -1;
}

// --- closing ---------------------------------------------------------------------------------------------------------

static void* ReapWhenEnded(void* id)
{
	int status = 0;
	while (waitpid((pid_t)(intptr_t)id, &status, 0) < 0 && errno == EINTR) {}
	return NULL;
}

// Reaps the child: now when it has ended, else on a thread of its own that waits for it, so it never stays a zombie.
// The thread keeps the default stack, which glibc sizes for the program's thread-locals (a small one can fail to
// start). When no thread can be started the child stays a zombie until this process ends.
static void Reap(pid_t id)
{
	int status = 0;
	pid_t reaped;
	do reaped = waitpid(id, &status, WNOHANG);
	while (reaped < 0 && errno == EINTR);
	if (reaped != 0) return;
	pthread_attr_t attributes;
	if (pthread_attr_init(&attributes) != 0) return;
	pthread_t thread;
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	pthread_create(&thread, &attributes, ReapWhenEnded, (void*)(intptr_t)id);
	pthread_attr_destroy(&attributes);
}

void c4_process_close(c4_process* process)
{
	if (!process) return;
	for (int i = 0; i < 3; ++i) c4_process_posix_close(&process->pipes[i]);
	c4_process_posix_close(&process->console);
	c4_process_posix_child* child = (c4_process_posix_child*)process->process;
	process->process = NULL;
	process->id = 0;
	if (!child) return;
	if (child->killOnClose) c4_process_posix_kill_tree(child->id, child->leadsSession);
	if (child->pidfd >= 0) close(child->pidfd);
	Reap(child->id);
	free(child);
}
