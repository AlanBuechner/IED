// The pipes between a parent and its child (ProcessPosix.h, ProcessRuntimeC.h). The parent's end of each is
// non-blocking: a read or write is tried, then waited for in poll, so it can give up after a timeout. A write to a
// pipe whose reader is gone fails (EPIPE) rather than raising SIGPIPE, which would end this process. A terminal's ends
// are descriptors of its pseudo-terminal's master, which says EIO, not end of file, once every process has closed the
// terminal: that is its end.

#include "ProcessPosix.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { kLargestTransfer = 0x40000000, kFirstFreeDescriptor = 3 };

int64_t c4_process_posix_deadline(int32_t timeoutMs)
{
	if (timeoutMs < 0) return -1;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000 + timeoutMs;
}

int32_t c4_process_posix_remaining(int64_t deadline)
{
	if (deadline < 0) return -1;
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	const int64_t left = deadline - ((int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
	return left > 0 ? (int32_t)left : 0;
}

int c4_process_posix_raise(int fd)
{
	if (fd < 0 || fd >= kFirstFreeDescriptor) return fd;
	const int raised = fcntl(fd, F_DUPFD_CLOEXEC, kFirstFreeDescriptor);
	const int why = errno;
	close(fd);
	errno = why;
	return raised;
}

bool c4_process_posix_pipe(bool parentReads, int* parent, int* child)
{
	int ends[2];
	*parent = *child = -1;
	if (pipe2(ends, O_CLOEXEC) != 0) return false;
	const int reader = c4_process_posix_raise(ends[0]);
	const int writer = c4_process_posix_raise(ends[1]);
	const int mine = parentReads ? reader : writer;
	if (reader >= 0 && writer >= 0 && fcntl(mine, F_SETFL, O_NONBLOCK) == 0)
	{
		*parent = mine;
		*child = parentReads ? writer : reader;
		return true;
	}
	const int why = errno;
	if (reader >= 0) close(reader);
	if (writer >= 0) close(writer);
	errno = why;
	return false;
}

void c4_process_posix_close(void** handle)
{
	const int fd = c4_process_posix_fd(*handle);
	if (fd >= 0) close(fd);
	*handle = NULL;
}

// --- reading and writing ---------------------------------------------------------------------------------------------

// A write with SIGPIPE blocked in this thread, so a reader that is gone is EPIPE; the SIGPIPE it raised is taken
// before the signal is unblocked, unless one was pending already.
static ssize_t WriteQuietly(int fd, const void* data, size_t size)
{
	sigset_t pipeSignal;
	sigset_t before;
	sigset_t pending;
	sigemptyset(&pipeSignal);
	sigaddset(&pipeSignal, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &pipeSignal, &before);
	sigpending(&pending);
	const bool wasPending = sigismember(&pending, SIGPIPE) == 1;
	const ssize_t written = write(fd, data, size);
	const int why = errno;
	if (written < 0 && why == EPIPE && !wasPending)
	{
		const struct timespec none = { 0, 0 };
		while (sigtimedwait(&pipeSignal, NULL, &none) < 0 && errno == EINTR) {}
	}
	pthread_sigmask(SIG_SETMASK, &before, NULL);
	errno = why;
	return written;
}

// Waits until `fd` can be read (POLLIN) or written (POLLOUT) without blocking, or has ended: 1, 0 once `deadline`
// passed, or -1.
static int WaitFor(int fd, short events, int64_t deadline)
{
	for (;;)
	{
		struct pollfd ready = { fd, events, 0 };
		const int polled = poll(&ready, 1, c4_process_posix_remaining(deadline));
		if (polled >= 0) return polled > 0 ? 1 : 0;
		if (errno != EINTR) return -1;
	}
}

// One read or write, given up after `timeoutMs`: the bytes moved, 0 at the end of a pipe the other side closed (a
// write there is -1), kProcessTimedOut, or -1.
static int64_t Transfer(int fd, bool writing, void* data, int64_t size, int32_t timeoutMs)
{
	if (fd < 0) return kProcessError;
	const size_t chunk = size > kLargestTransfer ? kLargestTransfer : (size_t)size;
	const int64_t deadline = c4_process_posix_deadline(timeoutMs);
	for (;;)
	{
		const ssize_t moved = writing ? WriteQuietly(fd, data, chunk) : read(fd, data, chunk);
		if (moved > 0 || (moved == 0 && !writing)) return moved;
		if (moved < 0 && !writing && errno == EIO) return 0;
		if (moved == 0 || (errno != EAGAIN && errno != EINTR)) return kProcessError;
		const int ready = WaitFor(fd, writing ? POLLOUT : POLLIN, deadline);
		if (ready == 0) return kProcessTimedOut;
		if (ready < 0) return kProcessError;
	}
}

int64_t c4_process_write(c4_process* process, const void* data, int64_t size, int32_t timeoutMs)
{
	if (!process || size < 0 || (size > 0 && !data)) return kProcessError;
	if (size == 0) return process->pipes[kProcessStdin] ? 0 : kProcessError;
	return Transfer(c4_process_posix_fd(process->pipes[kProcessStdin]), true, (void*)data, size, timeoutMs);
}

int64_t c4_process_read(c4_process* process, int32_t stream, void* buffer, int64_t capacity, int32_t timeoutMs)
{
	if (!process || (stream != kProcessStdout && stream != kProcessStderr) || capacity <= 0 || !buffer) return kProcessError;
	return Transfer(c4_process_posix_fd(process->pipes[stream]), false, buffer, capacity, timeoutMs);
}

// Which of `fds` can be read without blocking (it has bytes, or has ended): its index, kProcessTimedOut, or -1.
static int32_t WaitReadable(const int* fds, int32_t count, int32_t timeoutMs)
{
	const int64_t deadline = c4_process_posix_deadline(timeoutMs);
	for (;;)
	{
		struct pollfd ready[2];
		for (int32_t i = 0; i < count; ++i)
		{
			ready[i].fd = fds[i];
			ready[i].events = POLLIN;
			ready[i].revents = 0;
		}
		const int polled = poll(ready, (nfds_t)count, c4_process_posix_remaining(deadline));
		if (polled == 0) return kProcessTimedOut;
		if (polled < 0 && errno != EINTR) return kProcessError;
		for (int32_t i = 0; polled > 0 && i < count; ++i)
			if (ready[i].revents) return i;
	}
}

int64_t c4_process_read_any(c4_process* process, void* buffer, int64_t capacity, int32_t timeoutMs, int32_t* stream)
{
	if (!process || !buffer || capacity <= 0 || !stream) return kProcessError;
	*stream = -1;
	int fds[2];
	int32_t streams[2];
	int32_t count = 0;
	for (int32_t s = kProcessStdout; s <= kProcessStderr; ++s)
		if (process->pipes[s])
		{
			fds[count] = c4_process_posix_fd(process->pipes[s]);
			streams[count++] = s;
		}
	if (count == 0) return 0;
	const int32_t ready = WaitReadable(fds, count, timeoutMs);
	if (ready < 0) return ready;
	*stream = streams[ready];
	const int64_t read = Transfer(fds[ready], false, buffer, capacity, kProcessWaitForever);
	if (read == 0) c4_process_posix_close(&process->pipes[*stream]);
	return read;
}

void c4_process_close_pipe(c4_process* process, int32_t stream)
{
	if (process && stream >= 0 && stream < 3) c4_process_posix_close(&process->pipes[stream]);
}
