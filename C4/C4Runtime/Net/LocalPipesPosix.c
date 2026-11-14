// Local connections over AF_UNIX stream sockets (LocalRuntimeC.h). A name is an address in Linux's abstract namespace,
// "\0c4-local/<name>", not a file: it is gone the moment its listener closes or its process dies (a crash leaves no
// socket file behind to refuse the next listener), and every process of the machine finds it whatever its environment
// says ($XDG_RUNTIME_DIR, $TMPDIR differ between a login, a service and sudo). Processes in another network namespace
// (a container) do not see it, as another session's pipes are out of reach on Windows. The abstract namespace has no
// permissions, so each end checks the other's user (SO_PEERCRED): a listener drops another user's client unaccepted,
// and a client refuses a listener of another user - what the pipe's access list and ListenerIsOurs do on Windows. A
// connection is a byte stream, as the byte-mode pipe is; a listener's backlog lets many clients connect before one is
// accepted. Every wait polls with its timeout, and every transfer moves what fits at once (MSG_DONTWAIT).

#include "LocalRuntimeC.h"
#include "NetRuntimeC.h"
#include "SystemErrorC.h"
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static const char kNamePrefix[] = "c4-local/";

enum
{
	kLargestTransfer = 0x40000000,
	kPrefixBytes = sizeof(kNamePrefix) - 1,
	// sun_path holds the abstract address's leading zero, the prefix and the name.
	kLongestName = sizeof(((struct sockaddr_un*)0)->sun_path) - 1 - kPrefixBytes,
	kUserIdBytes = 16,
};

struct c4_local_pipe
{
	int socket;
	bool listening;   // a listener's socket; else a connection's
};

static void NoError(char* error, int64_t capacity) { c4_copy_text("", error, capacity); }

static c4_local_pipe* Failed(int code, char* error, int64_t capacity)
{
	c4_system_error_text((uint32_t)code, error, capacity);
	return NULL;
}

static c4_local_pipe* Refused(const char* why, char* error, int64_t capacity)
{
	c4_copy_text(why, error, capacity);
	return NULL;
}

// The abstract address of `name`: its length, or 0 for a name that is empty, has a backslash (refused on Windows, so a
// name means the same on both) or does not fit.
static socklen_t AddressOf(const char* name, struct sockaddr_un* out)
{
	memset(out, 0, sizeof(*out));
	if (!name || !name[0] || strchr(name, '\\')) return 0;
	const size_t length = strlen(name);
	if (length > kLongestName) return 0;
	out->sun_family = AF_UNIX;
	memcpy(out->sun_path + 1, kNamePrefix, kPrefixBytes);
	memcpy(out->sun_path + 1 + kPrefixBytes, name, length);
	return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + kPrefixBytes + length);
}

static c4_local_pipe* Wrap(int socket, bool listening)
{
	c4_local_pipe* pipe = (c4_local_pipe*)calloc(1, sizeof(c4_local_pipe));
	if (!pipe)
	{
		close(socket);
		return NULL;
	}
	pipe->socket = socket;
	pipe->listening = listening;
	return pipe;
}

// Whether the process at the other end of `socket` runs as the current user (its effective user, as SO_PEERCRED says).
static bool PeerIsOurs(int socket)
{
	struct ucred peer;
	socklen_t size = sizeof(peer);
	return getsockopt(socket, SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 && peer.uid == geteuid();
}

// --- time ------------------------------------------------------------------------------------------------------------

static int64_t NowMs(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

// -1 for ever (a negative timeout), else when the wait ends.
static int64_t DeadlineAfter(int32_t timeoutMs) { return timeoutMs < 0 ? -1 : NowMs() + timeoutMs; }

// What is left of a deadline: -1 for ever, else milliseconds (0 once it passed).
static int Left(int64_t deadline)
{
	if (deadline < 0) return -1;
	const int64_t left = deadline - NowMs();
	return left <= 0 ? 0 : left > INT_MAX ? INT_MAX : (int)left;
}

// 1 once `socket` is ready for `events` (or hung up: the call that follows says how), 0 when `deadline` passed first,
// -1 when waiting failed or the socket is not open; resumed after a signal.
static int WaitReady(int socket, short events, int64_t deadline)
{
	struct pollfd entry = { socket, events, 0 };
	for (;;)
	{
		const int ready = poll(&entry, 1, Left(deadline));
		if (ready > 0 && (entry.revents & POLLNVAL))
		{
			errno = EBADF;
			return -1;
		}
		if (ready > 0) return 1;
		if (ready == 0) return 0;
		if (errno != EINTR) return -1;
	}
}

// --- Listening -------------------------------------------------------------------------------------------------------

c4_local_pipe* c4_local_listen(const char* name, char* error, int64_t errorCapacity)
{
	NoError(error, errorCapacity);
	struct sockaddr_un at;
	const socklen_t length = AddressOf(name, &at);
	if (!length) return Refused("not a local name: one that is not empty, has no backslash and fits 98 bytes", error, errorCapacity);
	// Non-blocking, so an accept after poll never waits on a connection another user's client made and dropped.
	const int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (s < 0) return Failed(errno, error, errorCapacity);
	if (bind(s, (const struct sockaddr*)&at, length) < 0 || listen(s, SOMAXCONN) < 0)
	{
		const int why = errno;
		close(s);
		if (why == EADDRINUSE) return Refused("the name is taken: something listens there already", error, errorCapacity);
		return Failed(why, error, errorCapacity);
	}
	return Wrap(s, true);
}

c4_local_pipe* c4_local_accept(c4_local_pipe* listener, int32_t timeoutMs, int32_t* timedOut, char* error, int64_t errorCapacity)
{
	*timedOut = 0;
	NoError(error, errorCapacity);
	if (!listener || !listener->listening) return Refused("not a listener", error, errorCapacity);
	const int64_t deadline = DeadlineAfter(timeoutMs);
	for (;;)
	{
		const int ready = WaitReady(listener->socket, POLLIN, deadline);
		if (ready < 0) return Failed(errno, error, errorCapacity);
		if (ready == 0)
		{
			*timedOut = 1;
			return Refused("no client came in time", error, errorCapacity);
		}
		const int s = accept4(listener->socket, NULL, NULL, SOCK_CLOEXEC);
		if (s < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ECONNABORTED) continue;
			return Failed(errno, error, errorCapacity);
		}
		if (PeerIsOurs(s)) return Wrap(s, false);
		close(s);   // another user's client: never accepted
	}
}

// --- Connecting and talking ------------------------------------------------------------------------------------------

// Connects `s` to `at` by `deadline`: a listener whose backlog is full keeps a connect waiting, as long as SO_SNDTIMEO
// says (EAGAIN once it passed; 0 is for ever, so a wait that is over gets 1 ms). 0, or errno.
static int ConnectBy(int s, const struct sockaddr_un* at, socklen_t length, int64_t deadline)
{
	for (;;)
	{
		const int left = Left(deadline);
		const int ms = left < 0 ? 0 : left > 0 ? left : 1;
		const struct timeval wait = { ms / 1000, (ms % 1000) * 1000 };
		setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
		if (connect(s, (const struct sockaddr*)at, length) == 0) return 0;
		if (errno != EINTR) return errno;
	}
}

c4_local_pipe* c4_local_connect(const char* name, int32_t timeoutMs, int32_t* timedOut, char* error, int64_t errorCapacity)
{
	*timedOut = 0;
	NoError(error, errorCapacity);
	struct sockaddr_un at;
	const socklen_t length = AddressOf(name, &at);
	if (!length) return Refused("not a local name", error, errorCapacity);
	const int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (s < 0) return Failed(errno, error, errorCapacity);
	const int why = ConnectBy(s, &at, length, DeadlineAfter(timeoutMs));
	if (why == 0 && PeerIsOurs(s)) return Wrap(s, false);
	close(s);
	if (why == 0) return Refused("the listener on that name runs as another user", error, errorCapacity);
	if (why == ECONNREFUSED || why == ENOENT) return Refused("no one listens on that name", error, errorCapacity);
	if (why == EAGAIN || why == EWOULDBLOCK)
	{
		*timedOut = 1;
		return Refused("the listener stayed busy", error, errorCapacity);
	}
	return Failed(why, error, errorCapacity);
}

// One send or receive of what can move at once, once the socket is ready within `timeoutMs`.
static int64_t Transfer(c4_local_pipe* pipe, bool writing, void* data, int64_t size, int32_t timeoutMs)
{
	if (!pipe || pipe->listening || size < 0 || (size > 0 && !data)) return kNetError;
	if (size == 0) return 0;
	const size_t chunk = size > kLargestTransfer ? (size_t)kLargestTransfer : (size_t)size;
	const int64_t deadline = DeadlineAfter(timeoutMs);
	for (;;)
	{
		const int ready = WaitReady(pipe->socket, writing ? POLLOUT : POLLIN, deadline);
		if (ready < 0) return kNetError;
		if (ready == 0) return kNetTimedOut;
		const ssize_t moved = writing ? send(pipe->socket, data, chunk, MSG_NOSIGNAL | MSG_DONTWAIT)
			: recv(pipe->socket, data, chunk, MSG_DONTWAIT);
		if (moved >= 0) return moved;
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
		// The other side closed without reading all it was sent: a receive reads that as the end, as a broken pipe
		// reads on Windows; a send fails.
		return !writing && errno == ECONNRESET ? 0 : kNetError;
	}
}

int64_t c4_local_send(c4_local_pipe* pipe, const void* data, int64_t size, int32_t timeoutMs)
{
	return Transfer(pipe, true, (void*)data, size, timeoutMs);
}

int64_t c4_local_receive(c4_local_pipe* pipe, void* buffer, int64_t capacity, int32_t timeoutMs)
{
	return Transfer(pipe, false, buffer, capacity, timeoutMs);
}

void c4_local_close(c4_local_pipe* pipe)
{
	if (!pipe) return;
	close(pipe->socket);
	free(pipe);
}

// The effective user's id in decimal ("1000"): the user SO_PEERCRED reports to the other end.
int64_t c4_local_user_id(char* out, int64_t capacity)
{
	char text[kUserIdBytes];
	const int length = snprintf(text, sizeof(text), "%u", (unsigned)geteuid());
	c4_copy_text(text, out, capacity);
	return length;
}
