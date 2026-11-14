// Sockets for Bedrock's Net namespace (NetRuntimeC.h) over BSD sockets. A call with a timeout first waits with poll;
// a connect runs non-blocking and waits with poll, then reads why it failed from SO_ERROR. Sends pass MSG_NOSIGNAL, so
// writing to a peer that has gone fails the send instead of raising SIGPIPE. A wait a signal interrupts (EINTR, as a
// collector stopping the world causes) resumes with what is left of its time. A socket handle is the descriptor.

#include "NetRuntimeC.h"
#include "SystemErrorC.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

enum { kMaxPort = 65535, kHostTextBytes = 64, kResolveTextBytes = 256 };

// A descriptor no process has: a handle that is not a socket fails as a closed one does (EBADF; POLLNVAL in a poll).
enum { kNoDescriptor = INT_MAX };

static int DescriptorOf(int64_t socket) { return socket >= 0 && socket < kNoDescriptor ? (int)socket : kNoDescriptor; }

// --- results ---------------------------------------------------------------------------------------------------------

static int64_t FailedWith(int code, char* error, int64_t capacity)
{
	c4_system_error_text((uint32_t)code, error, capacity);
	return kNetError;
}

static int64_t Failed(char* error, int64_t capacity) { return FailedWith(errno, error, capacity); }

static int64_t Refused(const char* why, char* error, int64_t capacity)
{
	c4_copy_text(why, error, capacity);
	return kNetError;
}

static int64_t TimedOut(char* error, int64_t capacity)
{
	c4_copy_text("timed out", error, capacity);
	return kNetTimedOut;
}

static int64_t Done(int64_t result, char* error, int64_t capacity)
{
	c4_copy_text("", error, capacity);
	return result;
}

// --- time ------------------------------------------------------------------------------------------------------------

static int64_t NowMs(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

// A wait of `timeoutMs` as a deadline: -1 for ever (kNetWaitForever), else when it ends (a negative wait is none).
static int64_t DeadlineAfter(int32_t timeoutMs)
{
	if (timeoutMs == kNetWaitForever) return -1;
	return NowMs() + (timeoutMs > 0 ? timeoutMs : 0);
}

// What is left of a deadline for poll: -1 for ever, else milliseconds (0 once it passed).
static int Left(int64_t deadline)
{
	if (deadline < 0) return -1;
	const int64_t left = deadline - NowMs();
	return left <= 0 ? 0 : left > INT_MAX ? INT_MAX : (int)left;
}

// poll() until `deadline`, resumed after a signal: what poll returned.
static int PollUntil(struct pollfd* entries, nfds_t count, int64_t deadline)
{
	for (;;)
	{
		const int ready = poll(entries, count, Left(deadline));
		if (ready >= 0 || errno != EINTR) return ready;
	}
}

// --- addresses -------------------------------------------------------------------------------------------------------

// The system's form of `address`: its length, or 0 when it is not an IPv4 or IPv6 address with a port that fits.
static socklen_t ToSystem(const c4_net_address* address, struct sockaddr_storage* out)
{
	memset(out, 0, sizeof(*out));
	if (!address || address->port < 0 || address->port > kMaxPort) return 0;
	if (address->family == kNetIPv4)
	{
		struct sockaddr_in* in = (struct sockaddr_in*)out;
		in->sin_family = AF_INET;
		in->sin_port = htons((uint16_t)address->port);
		memcpy(&in->sin_addr, address->bytes, 4);
		return sizeof(*in);
	}
	if (address->family == kNetIPv6)
	{
		struct sockaddr_in6* in6 = (struct sockaddr_in6*)out;
		in6->sin6_family = AF_INET6;
		in6->sin6_port = htons((uint16_t)address->port);
		memcpy(&in6->sin6_addr, address->bytes, 16);
		in6->sin6_scope_id = address->scope;
		return sizeof(*in6);
	}
	return 0;
}

static void FromSystem(const struct sockaddr* from, c4_net_address* out)
{
	memset(out, 0, sizeof(*out));
	if (from->sa_family == AF_INET)
	{
		const struct sockaddr_in* in = (const struct sockaddr_in*)from;
		out->family = kNetIPv4;
		out->port = ntohs(in->sin_port);
		memcpy(out->bytes, &in->sin_addr, 4);
	}
	else if (from->sa_family == AF_INET6)
	{
		const struct sockaddr_in6* in6 = (const struct sockaddr_in6*)from;
		out->family = kNetIPv6;
		out->port = ntohs(in6->sin6_port);
		memcpy(out->bytes, &in6->sin6_addr, 16);
		out->scope = in6->sin6_scope_id;
	}
}

static const char* kNotAnAddress = "not an IPv4 or IPv6 address with a port from 0 to 65535";

// getaddrinfo's failures are codes of its own (EAI_*) with messages of their own; EAI_SYSTEM's is errno's.
static int64_t ResolveFailed(int status, char* error, int64_t capacity)
{
	if (status == EAI_SYSTEM) return Failed(error, capacity);
	char text[kResolveTextBytes];
	snprintf(text, sizeof(text), "%s (error %d)", gai_strerror(status), status);
	return Refused(text, error, capacity);
}

int32_t c4_net_resolve(const char* host, int32_t port, c4_net_address* out, int32_t capacity, char* error, int64_t errorCapacity)
{
	if (!host || !*host || port < 0 || port > kMaxPort || capacity < 0 || (capacity > 0 && !out))
		return (int32_t)Refused("a host name and a port from 0 to 65535 are needed", error, errorCapacity);
	char service[8];
	snprintf(service, sizeof(service), "%d", port);
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo* list = NULL;
	const int status = getaddrinfo(host, service, &hints, &list);
	if (status != 0) return (int32_t)ResolveFailed(status, error, errorCapacity);
	int32_t count = 0;
	for (const struct addrinfo* entry = list; entry && count < capacity; entry = entry->ai_next)
		if (entry->ai_family == AF_INET || entry->ai_family == AF_INET6) FromSystem(entry->ai_addr, &out[count++]);
	freeaddrinfo(list);
	return (int32_t)Done(count, error, errorCapacity);
}

int64_t c4_net_address_text(const c4_net_address* address, char* out, int64_t capacity)
{
	char host[kHostTextBytes] = "";
	char text[kHostTextBytes + 32] = "";
	if (address && address->family == kNetIPv4 && inet_ntop(AF_INET, address->bytes, host, sizeof(host)))
		snprintf(text, sizeof(text), "%s:%d", host, address->port);
	else if (address && address->family == kNetIPv6 && inet_ntop(AF_INET6, address->bytes, host, sizeof(host)))
	{
		if (address->scope) snprintf(text, sizeof(text), "[%s%%%u]:%d", host, address->scope, address->port);
		else snprintf(text, sizeof(text), "[%s]:%d", host, address->port);
	}
	const int64_t length = (int64_t)strlen(text);
	if (out && capacity > length) memcpy(out, text, (size_t)length + 1);
	return length;
}

// Decimal digits only, up to `limit`.
static bool ParseNumber(const char* text, int64_t limit, int64_t* value)
{
	if (!*text) return false;
	int64_t number = 0;
	for (const char* c = text; *c; ++c)
	{
		if (*c < '0' || *c > '9') return false;
		number = number * 10 + (*c - '0');
		if (number > limit) return false;
	}
	*value = number;
	return true;
}

// Splits "host", "host:port", "[host]" or "[host]:port" into `host` and `port` text; false when malformed.
static bool SplitHostPort(const char* text, char* host, const char** port, int32_t* family)
{
	const char* hostStart = text;
	size_t hostLength;
	*port = NULL;
	if (text[0] == '[')
	{
		const char* close = strchr(text, ']');
		if (!close || (close[1] != 0 && close[1] != ':')) return false;
		hostStart = text + 1;
		hostLength = (size_t)(close - hostStart);
		if (close[1] == ':') *port = close + 2;
		*family = kNetIPv6;
	}
	else
	{
		const char* colon = strchr(text, ':');
		const bool bareIPv6 = colon && strchr(colon + 1, ':');
		hostLength = colon && !bareIPv6 ? (size_t)(colon - text) : strlen(text);
		if (colon && !bareIPv6) *port = colon + 1;
		*family = bareIPv6 ? kNetIPv6 : kNetIPv4;
	}
	if (hostLength == 0 || hostLength >= kHostTextBytes) return false;
	memcpy(host, hostStart, hostLength);
	host[hostLength] = 0;
	return true;
}

int32_t c4_net_parse_address(const char* text, c4_net_address* out)
{
	if (!out) return kNetError;
	memset(out, 0, sizeof(*out));
	char host[kHostTextBytes];
	const char* portText;
	int32_t family;
	if (!text || !SplitHostPort(text, host, &portText, &family)) return kNetError;
	int64_t number = 0;
	if (portText && !ParseNumber(portText, kMaxPort, &number)) return kNetError;
	char* scope = family == kNetIPv6 ? strchr(host, '%') : NULL;
	int64_t scopeId = 0;
	if (scope)
	{
		*scope = 0;
		if (!ParseNumber(scope + 1, UINT_MAX, &scopeId)) return kNetError;
	}
	if (inet_pton(family == kNetIPv4 ? AF_INET : AF_INET6, host, out->bytes) != 1)
	{
		memset(out, 0, sizeof(*out));
		return kNetError;
	}
	out->family = family;
	out->port = (int32_t)number;
	out->scope = (uint32_t)scopeId;
	return 0;
}

// --- sockets ---------------------------------------------------------------------------------------------------------

// Not inherited: a program the process starts never holds its connections open. An IPv6 socket carries IPv6 only, as
// on Windows (Linux's default is both), so [::] and 0.0.0.0 can be bound side by side.
static int NewSocket(int family, int type, int protocol)
{
	const int s = socket(family, type | SOCK_CLOEXEC, protocol);
	if (s >= 0 && family == AF_INET6)
	{
		const int only = 1;
		setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &only, sizeof(only));
	}
	return s;
}

// Before a call that would block: 1 when the socket is ready for `events` (or failed: the call then says how), 0 on a
// timeout, -1 on an error. Waiting forever is left to the call itself.
static int WaitFor(int socket, short events, int32_t timeoutMs)
{
	if (timeoutMs == kNetWaitForever) return 1;
	struct pollfd entry = { socket, events, 0 };
	const int ready = PollUntil(&entry, 1, DeadlineAfter(timeoutMs));
	return ready < 0 ? -1 : ready > 0 ? 1 : 0;
}

static size_t Chunk(int64_t size) { return size > INT_MAX ? (size_t)INT_MAX : (size_t)size; }

// The most a TCP send with a timeout hands the system at once, so its wait bounds how long the peer may take nothing.
enum { kTimedSendBytes = 64 * 1024 };

int64_t c4_net_tcp_listen(const c4_net_address* address, int32_t backlog, char* error, int64_t errorCapacity)
{
	struct sockaddr_storage at;
	const socklen_t length = ToSystem(address, &at);
	if (!length) return Refused(kNotAnAddress, error, errorCapacity);
	const int s = NewSocket(at.ss_family, SOCK_STREAM, IPPROTO_TCP);
	if (s < 0) return Failed(error, errorCapacity);
	// A port whose last connections linger in TIME_WAIT can be listened on again at once. Unlike Windows' option of the
	// same name, Linux's still refuses a second listener on a port that has one.
	const int reuse = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	if (bind(s, (const struct sockaddr*)&at, length) < 0 || listen(s, backlog > 0 ? backlog : SOMAXCONN) < 0)
	{
		const int64_t result = Failed(error, errorCapacity);
		close(s);
		return result;
	}
	return Done(s, error, errorCapacity);
}

int64_t c4_net_tcp_accept(int64_t listener, int32_t timeoutMs, c4_net_address* peer, char* error, int64_t errorCapacity)
{
	const int ready = WaitFor(DescriptorOf(listener), POLLIN, timeoutMs);
	if (ready < 0) return Failed(error, errorCapacity);
	if (ready == 0) return TimedOut(error, errorCapacity);
	struct sockaddr_storage from;
	socklen_t length;
	int s;
	do
	{
		memset(&from, 0, sizeof(from));
		length = sizeof(from);
		s = accept4(DescriptorOf(listener), (struct sockaddr*)&from, &length, SOCK_CLOEXEC);
	} while (s < 0 && errno == EINTR);
	if (s < 0) return Failed(error, errorCapacity);
	if (peer) FromSystem((const struct sockaddr*)&from, peer);
	return Done(s, error, errorCapacity);
}

// A connect that gives up at `deadline`: non-blocking until poll says the socket is connected or failed (refused,
// unreachable; SO_ERROR says which), then blocking again. A connect a signal interrupts goes on, so it is waited for
// the same way.
static int64_t ConnectBy(int s, const struct sockaddr* to, socklen_t length, int64_t deadline, char* error, int64_t capacity)
{
	const int flags = fcntl(s, F_GETFL);
	if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) return Failed(error, capacity);
	if (connect(s, to, length) < 0)
	{
		if (errno != EINPROGRESS && errno != EINTR) return Failed(error, capacity);
		struct pollfd entry = { s, POLLOUT, 0 };
		const int ready = PollUntil(&entry, 1, deadline);
		if (ready < 0) return Failed(error, capacity);
		if (ready == 0) return TimedOut(error, capacity);
		int code = 0;
		socklen_t size = sizeof(code);
		if (getsockopt(s, SOL_SOCKET, SO_ERROR, &code, &size) < 0) return Failed(error, capacity);
		if (code != 0) return FailedWith(code, error, capacity);
	}
	if (fcntl(s, F_SETFL, flags) < 0) return Failed(error, capacity);
	return Done(s, error, capacity);
}

int64_t c4_net_tcp_connect(const c4_net_address* address, int32_t timeoutMs, char* error, int64_t errorCapacity)
{
	struct sockaddr_storage to;
	const socklen_t length = ToSystem(address, &to);
	if (!length || address->port == 0) return Refused(kNotAnAddress, error, errorCapacity);
	const int s = NewSocket(to.ss_family, SOCK_STREAM, IPPROTO_TCP);
	if (s < 0) return Failed(error, errorCapacity);
	const int64_t result = ConnectBy(s, (const struct sockaddr*)&to, length, DeadlineAfter(timeoutMs), error, errorCapacity);
	if (result < 0) close(s);
	return result;
}

int32_t c4_net_set_no_delay(int64_t socket, int32_t on)
{
	const int value = on ? 1 : 0;
	return setsockopt(DescriptorOf(socket), IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value)) < 0 ? kNetError : 0;
}

int64_t c4_net_udp_open(const c4_net_address* address, char* error, int64_t errorCapacity)
{
	struct sockaddr_storage at;
	const socklen_t length = ToSystem(address, &at);
	if (!length) return Refused(kNotAnAddress, error, errorCapacity);
	const int s = NewSocket(at.ss_family, SOCK_DGRAM, IPPROTO_UDP);
	if (s < 0) return Failed(error, errorCapacity);
	if (bind(s, (const struct sockaddr*)&at, length) < 0)
	{
		const int64_t result = Failed(error, errorCapacity);
		close(s);
		return result;
	}
	return Done(s, error, errorCapacity);
}

// How long a blocking send may wait for room (SO_SNDTIMEO): for ever, or what is left of `deadline` (at least 1 ms,
// since 0 means for ever).
static void LimitSendWait(int s, int64_t deadline)
{
	const int left = Left(deadline);
	const int ms = left < 0 ? 0 : left > 0 ? left : 1;
	const struct timeval wait = { ms / 1000, (ms % 1000) * 1000 };
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
}

int64_t c4_net_send(int64_t socket, const void* data, int64_t size, const c4_net_address* to, int32_t timeoutMs,
	char* error, int64_t errorCapacity)
{
	if (size < 0 || (size > 0 && !data)) return Refused("nothing to send", error, errorCapacity);
	struct sockaddr_storage destination;
	const socklen_t destinationLength = to ? ToSystem(to, &destination) : 0;
	if (to && !destinationLength) return Refused(kNotAnAddress, error, errorCapacity);
	const int s = DescriptorOf(socket);
	const int ready = WaitFor(s, POLLOUT, timeoutMs);
	if (ready < 0) return Failed(error, errorCapacity);
	if (ready == 0) return TimedOut(error, errorCapacity);
	// Writable means some room, not room for all of it: a blocking send of more than the room waits for the peer, so
	// with a timeout it waits at most that long for a piece of kTimedSendBytes (BUG-399), and returns what went by then.
	const int64_t deadline = DeadlineAfter(timeoutMs);
	const size_t piece = !to && timeoutMs != kNetWaitForever && size > kTimedSendBytes ? (size_t)kTimedSendBytes : Chunk(size);
	ssize_t sent;
	do
	{
		LimitSendWait(s, deadline);
		sent = to ? sendto(s, data, piece, MSG_NOSIGNAL, (const struct sockaddr*)&destination, destinationLength)
			: send(s, data, piece, MSG_NOSIGNAL);
	} while (sent < 0 && errno == EINTR);
	if (sent < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? TimedOut(error, errorCapacity) : Failed(error, errorCapacity);
	return Done(sent, error, errorCapacity);
}

// One recvmsg: what it returned, the message's flags in `*flags`.
static ssize_t ReceiveOnce(int s, void* buffer, size_t capacity, struct sockaddr_storage* sender, int how, int* flags)
{
	struct iovec piece = { buffer, capacity };
	struct msghdr message;
	memset(&message, 0, sizeof(message));
	memset(sender, 0, sizeof(*sender));
	message.msg_name = sender;
	message.msg_namelen = sizeof(*sender);
	message.msg_iov = &piece;
	message.msg_iovlen = 1;
	const ssize_t received = recvmsg(s, &message, how);
	*flags = message.msg_flags;
	return received;
}

int64_t c4_net_receive(int64_t socket, void* buffer, int64_t capacity, c4_net_address* from, int32_t timeoutMs,
	char* error, int64_t errorCapacity)
{
	if (capacity <= 0 || !buffer) return Refused("no room to receive into", error, errorCapacity);
	const int s = DescriptorOf(socket);
	const int64_t deadline = DeadlineAfter(timeoutMs);
	// With a timeout the receive itself never blocks: readable can be stale (a datagram dropped for its checksum), and
	// then it waits again with what is left.
	const int how = timeoutMs == kNetWaitForever ? 0 : MSG_DONTWAIT;
	struct sockaddr_storage sender;
	int flags = 0;
	ssize_t received;
	for (;;)
	{
		const int ready = WaitFor(s, POLLIN, deadline < 0 ? kNetWaitForever : Left(deadline));
		if (ready < 0) return Failed(error, errorCapacity);
		if (ready == 0) return TimedOut(error, errorCapacity);
		received = ReceiveOnce(s, buffer, Chunk(capacity), &sender, how, &flags);
		if (received >= 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) break;
	}
	if (received < 0) return Failed(error, errorCapacity);
	// A datagram larger than the buffer arrives cut to fit; Windows fails such a receive, and so does this.
	if (flags & MSG_TRUNC) return FailedWith(EMSGSIZE, error, errorCapacity);
	if (from) FromSystem((const struct sockaddr*)&sender, from);
	return Done(received, error, errorCapacity);
}

int32_t c4_net_local_address(int64_t socket, c4_net_address* out)
{
	struct sockaddr_storage at;
	memset(&at, 0, sizeof(at));
	socklen_t length = sizeof(at);
	if (!out || getsockname(DescriptorOf(socket), (struct sockaddr*)&at, &length) < 0) return kNetError;
	FromSystem((const struct sockaddr*)&at, out);
	return 0;
}

int32_t c4_net_peer_address(int64_t socket, c4_net_address* out)
{
	struct sockaddr_storage at;
	memset(&at, 0, sizeof(at));
	socklen_t length = sizeof(at);
	if (!out || getpeername(DescriptorOf(socket), (struct sockaddr*)&at, &length) < 0) return kNetError;
	FromSystem((const struct sockaddr*)&at, out);
	return 0;
}

// Both ways also ends a send, receive or poll another thread waits in on the socket: Linux wakes them all.
void c4_net_shutdown(int64_t socket, int32_t how)
{
	shutdown(DescriptorOf(socket), how == kNetShutdownReceive ? SHUT_RD : how == kNetShutdownSend ? SHUT_WR : SHUT_RDWR);
}

void c4_net_close(int64_t socket)
{
	if (socket >= 0) close(DescriptorOf(socket));
}

// POLLRDHUP is the peer's FIN, which Windows reports as POLLHUP: both are kNetClosed.
static int32_t ReadinessOf(short revents)
{
	return ((revents & (POLLIN | POLLPRI)) ? kNetReadable : 0) | ((revents & POLLOUT) ? kNetWritable : 0) |
		((revents & (POLLHUP | POLLRDHUP)) ? kNetClosed : 0) | ((revents & (POLLERR | POLLNVAL)) ? kNetFailed : 0);
}

int32_t c4_net_poll(c4_net_poll_entry* entries, int32_t count, int32_t timeoutMs)
{
	if (count <= 0 || !entries)
	{
		if (timeoutMs > 0) PollUntil(NULL, 0, DeadlineAfter(timeoutMs));
		return 0;
	}
	struct pollfd* polled = (struct pollfd*)calloc((size_t)count, sizeof(struct pollfd));
	if (!polled) return kNetError;
	for (int32_t i = 0; i < count; ++i)
	{
		polled[i].fd = DescriptorOf(entries[i].socket);
		polled[i].events = (short)(((entries[i].wanted & kNetReadable) ? POLLIN : 0) | ((entries[i].wanted & kNetWritable) ? POLLOUT : 0) | POLLRDHUP);
	}
	const int ready = PollUntil(polled, (nfds_t)count, timeoutMs < 0 ? -1 : DeadlineAfter(timeoutMs));
	for (int32_t i = 0; i < count; ++i) entries[i].ready = ready > 0 ? ReadinessOf(polled[i].revents) : 0;
	free(polled);
	return ready < 0 ? kNetError : ready;
}
