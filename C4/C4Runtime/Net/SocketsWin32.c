// Sockets for Bedrock's Net namespace (NetRuntimeC.h) over Winsock 2. A call with a timeout first waits with WSAPoll;
// a connect with a timeout runs non-blocking and waits with select, which reports a refused connection. Winsock
// starts the first time it is needed and stays up for the process.

#include "NetRuntimeC.h"
#include "PathRuntimeC.h"
#include "SystemErrorC.h"
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)   // mstcpip.h's, which not every SDK declares
#endif

enum { kMaxPort = 65535, kHostTextBytes = 64 };

static INIT_ONCE g_winsockOnce = INIT_ONCE_STATIC_INIT;
static int g_winsockError;

static BOOL CALLBACK StartWinsock(PINIT_ONCE once, PVOID parameter, PVOID* context)
{
	(void)once; (void)parameter; (void)context;
	WSADATA data;
	g_winsockError = WSAStartup(MAKEWORD(2, 2), &data);
	return TRUE;
}

static bool Started(char* error, int64_t capacity)
{
	InitOnceExecuteOnce(&g_winsockOnce, StartWinsock, NULL, NULL);
	if (g_winsockError == 0) return true;
	c4_system_error_text((uint32_t)g_winsockError, error, capacity);
	return false;
}

// --- results ---------------------------------------------------------------------------------------------------------

static int64_t Failed(char* error, int64_t capacity)
{
	c4_system_error_text((uint32_t)WSAGetLastError(), error, capacity);
	return kNetError;
}

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

// --- addresses -------------------------------------------------------------------------------------------------------

// The system's form of `address`: its length, or 0 when it is not an IPv4 or IPv6 address with a port that fits.
static int ToSystem(const c4_net_address* address, struct sockaddr_storage* out)
{
	memset(out, 0, sizeof(*out));
	if (!address || address->port < 0 || address->port > kMaxPort) return 0;
	if (address->family == kNetIPv4)
	{
		struct sockaddr_in* in = (struct sockaddr_in*)out;
		in->sin_family = AF_INET;
		in->sin_port = htons((u_short)address->port);
		memcpy(&in->sin_addr, address->bytes, 4);
		return sizeof(*in);
	}
	if (address->family == kNetIPv6)
	{
		struct sockaddr_in6* in6 = (struct sockaddr_in6*)out;
		in6->sin6_family = AF_INET6;
		in6->sin6_port = htons((u_short)address->port);
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

int32_t c4_net_resolve(const char* host, int32_t port, c4_net_address* out, int32_t capacity, char* error, int64_t errorCapacity)
{
	if (!Started(error, errorCapacity)) return kNetError;
	if (!host || !*host || port < 0 || port > kMaxPort || capacity < 0 || (capacity > 0 && !out))
		return (int32_t)Refused("a host name and a port from 0 to 65535 are needed", error, errorCapacity);
	wchar_t* wideHost = c4_utf8_to_wide(host);
	if (!wideHost) return (int32_t)Refused("the host name is not UTF-8", error, errorCapacity);
	wchar_t service[8];
	swprintf(service, 8, L"%d", port);
	ADDRINFOW hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	ADDRINFOW* list = NULL;
	const int status = GetAddrInfoW(wideHost, service, &hints, &list);
	free(wideHost);
	if (status != 0)
	{
		c4_system_error_text((uint32_t)status, error, errorCapacity);
		return kNetError;
	}
	int32_t count = 0;
	for (const ADDRINFOW* entry = list; entry && count < capacity; entry = entry->ai_next)
		if (entry->ai_family == AF_INET || entry->ai_family == AF_INET6) FromSystem(entry->ai_addr, &out[count++]);
	FreeAddrInfoW(list);
	return (int32_t)Done(count, error, errorCapacity);
}

int64_t c4_net_address_text(const c4_net_address* address, char* out, int64_t capacity)
{
	char host[kHostTextBytes] = "";
	char text[kHostTextBytes + 32] = "";
	if (Started(NULL, 0) && address && address->family == kNetIPv4 && inet_ntop(AF_INET, address->bytes, host, sizeof(host)))
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

// Decimal digits only, 0..65535.
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
	if (!text || !Started(NULL, 0) || !SplitHostPort(text, host, &portText, &family)) return kNetError;
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

// Not inherited: a child process the program starts never holds its connections open.
static SOCKET NewSocket(int family, int type, int protocol)
{
	return WSASocketW(family, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
}

// Before a call that would block: 1 when the socket is ready for `events`, 0 on a timeout, SOCKET_ERROR on an error.
// Waiting forever is left to the call itself.
static int WaitFor(SOCKET socket, short events, int32_t timeoutMs)
{
	if (timeoutMs == kNetWaitForever) return 1;
	WSAPOLLFD entry;
	entry.fd = socket;
	entry.events = events;
	entry.revents = 0;
	return WSAPoll(&entry, 1, timeoutMs < 0 ? 0 : timeoutMs);
}

static int Chunk(int64_t size) { return size > INT_MAX ? INT_MAX : (int)size; }

// The most a TCP send with a timeout hands the system at once, so its wait bounds how long the peer may take nothing.
enum { kTimedSendBytes = 64 * 1024 };

int64_t c4_net_tcp_listen(const c4_net_address* address, int32_t backlog, char* error, int64_t errorCapacity)
{
	if (!Started(error, errorCapacity)) return kNetError;
	struct sockaddr_storage at;
	const int length = ToSystem(address, &at);
	if (!length) return Refused(kNotAnAddress, error, errorCapacity);
	SOCKET s = NewSocket(at.ss_family, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) return Failed(error, errorCapacity);
	// No other socket may bind the same port while this one listens.
	BOOL exclusive = TRUE;
	setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof(exclusive));
	if (bind(s, (const struct sockaddr*)&at, length) == SOCKET_ERROR || listen(s, backlog > 0 ? backlog : SOMAXCONN) == SOCKET_ERROR)
	{
		const int64_t result = Failed(error, errorCapacity);
		closesocket(s);
		return result;
	}
	return Done((int64_t)s, error, errorCapacity);
}

int64_t c4_net_tcp_accept(int64_t listener, int32_t timeoutMs, c4_net_address* peer, char* error, int64_t errorCapacity)
{
	const int ready = WaitFor((SOCKET)listener, POLLRDNORM, timeoutMs);
	if (ready == SOCKET_ERROR) return Failed(error, errorCapacity);
	if (ready == 0) return TimedOut(error, errorCapacity);
	struct sockaddr_storage from;
	int length = sizeof(from);
	SOCKET s = accept((SOCKET)listener, (struct sockaddr*)&from, &length);
	if (s == INVALID_SOCKET) return Failed(error, errorCapacity);
	if (peer) FromSystem((const struct sockaddr*)&from, peer);
	return Done((int64_t)s, error, errorCapacity);
}

// A connect that gives up after `timeoutMs`: non-blocking until select says the socket is writable (connected) or
// failed (refused, unreachable), then blocking again.
static int64_t ConnectWithin(SOCKET s, const struct sockaddr* to, int length, int32_t timeoutMs, char* error, int64_t errorCapacity)
{
	u_long nonBlocking = 1;
	ioctlsocket(s, FIONBIO, &nonBlocking);
	if (connect(s, to, length) == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) return Failed(error, errorCapacity);
	fd_set writable, failed;
	FD_ZERO(&writable);
	FD_ZERO(&failed);
	FD_SET(s, &writable);
	FD_SET(s, &failed);
	const struct timeval wait = { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
	const int ready = select(0, NULL, &writable, &failed, &wait);
	if (ready == SOCKET_ERROR) return Failed(error, errorCapacity);
	if (ready == 0) return TimedOut(error, errorCapacity);
	if (FD_ISSET(s, &failed))
	{
		int code = 0;
		int size = sizeof(code);
		getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&code, &size);
		c4_system_error_text((uint32_t)code, error, errorCapacity);
		return kNetError;
	}
	u_long blocking = 0;
	ioctlsocket(s, FIONBIO, &blocking);
	return Done((int64_t)s, error, errorCapacity);
}

int64_t c4_net_tcp_connect(const c4_net_address* address, int32_t timeoutMs, char* error, int64_t errorCapacity)
{
	if (!Started(error, errorCapacity)) return kNetError;
	struct sockaddr_storage to;
	const int length = ToSystem(address, &to);
	if (!length || address->port == 0) return Refused(kNotAnAddress, error, errorCapacity);
	SOCKET s = NewSocket(to.ss_family, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) return Failed(error, errorCapacity);
	int64_t result;
	if (timeoutMs == kNetWaitForever)
		result = connect(s, (const struct sockaddr*)&to, length) == SOCKET_ERROR ? Failed(error, errorCapacity) : Done((int64_t)s, error, errorCapacity);
	else
		result = ConnectWithin(s, (const struct sockaddr*)&to, length, timeoutMs, error, errorCapacity);
	if (result < 0) closesocket(s);
	return result;
}

int32_t c4_net_set_no_delay(int64_t socket, int32_t on)
{
	const BOOL value = on ? TRUE : FALSE;
	return setsockopt((SOCKET)socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&value, sizeof(value)) == SOCKET_ERROR ? kNetError : 0;
}

int64_t c4_net_udp_open(const c4_net_address* address, char* error, int64_t errorCapacity)
{
	if (!Started(error, errorCapacity)) return kNetError;
	struct sockaddr_storage at;
	const int length = ToSystem(address, &at);
	if (!length) return Refused(kNotAnAddress, error, errorCapacity);
	SOCKET s = NewSocket(at.ss_family, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) return Failed(error, errorCapacity);
	// Windows otherwise fails the next receive when an earlier datagram's destination had no listener.
	BOOL reportResets = FALSE;
	DWORD returned = 0;
	WSAIoctl(s, SIO_UDP_CONNRESET, &reportResets, sizeof(reportResets), NULL, 0, &returned, NULL, NULL);
	if (bind(s, (const struct sockaddr*)&at, length) == SOCKET_ERROR)
	{
		const int64_t result = Failed(error, errorCapacity);
		closesocket(s);
		return result;
	}
	return Done((int64_t)s, error, errorCapacity);
}

int64_t c4_net_send(int64_t socket, const void* data, int64_t size, const c4_net_address* to, int32_t timeoutMs,
	char* error, int64_t errorCapacity)
{
	if (size < 0 || (size > 0 && !data)) return Refused("nothing to send", error, errorCapacity);
	struct sockaddr_storage destination;
	const int destinationLength = to ? ToSystem(to, &destination) : 0;
	if (to && !destinationLength) return Refused(kNotAnAddress, error, errorCapacity);
	const int ready = WaitFor((SOCKET)socket, POLLWRNORM, timeoutMs);
	if (ready == SOCKET_ERROR) return Failed(error, errorCapacity);
	if (ready == 0) return TimedOut(error, errorCapacity);
	// Writable means some room, not room for all of it: a blocking send of more than the room waits for the peer, so
	// with a timeout it waits at most that long for a piece of kTimedSendBytes (BUG-399).
	const DWORD sendWait = timeoutMs == kNetWaitForever ? 0 : (DWORD)(timeoutMs > 0 ? timeoutMs : 1);
	setsockopt((SOCKET)socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sendWait, sizeof(sendWait));
	const int piece = !to && timeoutMs != kNetWaitForever && size > kTimedSendBytes ? kTimedSendBytes : Chunk(size);
	const int sent = to ? sendto((SOCKET)socket, (const char*)data, piece, 0, (const struct sockaddr*)&destination, destinationLength)
		: send((SOCKET)socket, (const char*)data, piece, 0);
	if (sent == SOCKET_ERROR) return WSAGetLastError() == WSAETIMEDOUT ? TimedOut(error, errorCapacity) : Failed(error, errorCapacity);
	return Done(sent, error, errorCapacity);
}

int64_t c4_net_receive(int64_t socket, void* buffer, int64_t capacity, c4_net_address* from, int32_t timeoutMs,
	char* error, int64_t errorCapacity)
{
	if (capacity <= 0 || !buffer) return Refused("no room to receive into", error, errorCapacity);
	const int ready = WaitFor((SOCKET)socket, POLLRDNORM, timeoutMs);
	if (ready == SOCKET_ERROR) return Failed(error, errorCapacity);
	if (ready == 0) return TimedOut(error, errorCapacity);
	struct sockaddr_storage sender;
	int senderLength = sizeof(sender);
	const int received = from ? recvfrom((SOCKET)socket, (char*)buffer, Chunk(capacity), 0, (struct sockaddr*)&sender, &senderLength)
		: recv((SOCKET)socket, (char*)buffer, Chunk(capacity), 0);
	if (received == SOCKET_ERROR) return Failed(error, errorCapacity);
	if (from) FromSystem((const struct sockaddr*)&sender, from);
	return Done(received, error, errorCapacity);
}

int32_t c4_net_local_address(int64_t socket, c4_net_address* out)
{
	struct sockaddr_storage at;
	int length = sizeof(at);
	if (!out || getsockname((SOCKET)socket, (struct sockaddr*)&at, &length) == SOCKET_ERROR) return kNetError;
	FromSystem((const struct sockaddr*)&at, out);
	return 0;
}

int32_t c4_net_peer_address(int64_t socket, c4_net_address* out)
{
	struct sockaddr_storage at;
	int length = sizeof(at);
	if (!out || getpeername((SOCKET)socket, (struct sockaddr*)&at, &length) == SOCKET_ERROR) return kNetError;
	FromSystem((const struct sockaddr*)&at, out);
	return 0;
}

void c4_net_shutdown(int64_t socket, int32_t how)
{
	shutdown((SOCKET)socket, how == kNetShutdownReceive ? SD_RECEIVE : how == kNetShutdownSend ? SD_SEND : SD_BOTH);
	// Both ways: a send or receive another thread is blocked in returns now (shutdown alone leaves it waiting).
	if (how == kNetShutdownBoth) CancelIoEx((HANDLE)(uintptr_t)socket, NULL);
}

void c4_net_close(int64_t socket)
{
	if (socket >= 0) closesocket((SOCKET)socket);
}

static int32_t ReadinessOf(short revents)
{
	return ((revents & (POLLRDNORM | POLLRDBAND)) ? kNetReadable : 0) | ((revents & POLLWRNORM) ? kNetWritable : 0) |
		((revents & POLLHUP) ? kNetClosed : 0) | ((revents & (POLLERR | POLLNVAL)) ? kNetFailed : 0);
}

int32_t c4_net_poll(c4_net_poll_entry* entries, int32_t count, int32_t timeoutMs)
{
	if (count <= 0 || !entries)
	{
		if (timeoutMs > 0) Sleep((DWORD)timeoutMs);
		return 0;
	}
	if (!Started(NULL, 0)) return kNetError;
	WSAPOLLFD* polled = (WSAPOLLFD*)calloc((size_t)count, sizeof(WSAPOLLFD));
	if (!polled) return kNetError;
	for (int32_t i = 0; i < count; ++i)
	{
		polled[i].fd = (SOCKET)entries[i].socket;
		polled[i].events = (short)(((entries[i].wanted & kNetReadable) ? POLLRDNORM : 0) | ((entries[i].wanted & kNetWritable) ? POLLWRNORM : 0));
	}
	const int ready = WSAPoll(polled, (ULONG)count, timeoutMs < 0 ? -1 : timeoutMs);
	for (int32_t i = 0; i < count; ++i) entries[i].ready = ready > 0 ? ReadinessOf(polled[i].revents) : 0;
	free(polled);
	return ready == SOCKET_ERROR ? kNetError : ready;
}
