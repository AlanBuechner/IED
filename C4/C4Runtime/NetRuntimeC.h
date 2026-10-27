#pragma once
// The sockets behind Bedrock's Net namespace (D-65): TCP and UDP over IPv4 and IPv6, name resolution, readiness
// polling. One implementation file per OS (Net/SocketsWin32.c), so a port adds a file and no caller changes. A socket
// is an int64 handle, -1 for none. A call that can fail writes the system's message and code into `error` (UTF-8, cut
// at a whole character to `errorCapacity`, always terminated) and leaves it empty on success.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bedrock's Net::Address: an IPv4 or IPv6 address and a port, with the address bytes in network order (4 of the 16 used
// for IPv4).
typedef struct c4_net_address
{
	int32_t family;     // kNetIPv4 or kNetIPv6
	int32_t port;       // 0..65535
	uint8_t bytes[16];
	uint32_t scope;     // an IPv6 link-local address's interface, else 0
} c4_net_address;

enum { kNetIPv4 = 4, kNetIPv6 = 6 };

// Besides a count or a socket, a call returns one of these.
enum { kNetError = -1, kNetTimedOut = -2 };

// How long a call waits: milliseconds, 0 to not wait at all, or kNetWaitForever.
enum { kNetWaitForever = -1 };

enum { kNetShutdownReceive = 0, kNetShutdownSend = 1, kNetShutdownBoth = 2 };

// Bedrock's Net::Readiness bits: what an entry of c4_net_poll waits for, and what it found.
enum { kNetReadable = 1, kNetWritable = 2, kNetClosed = 4, kNetFailed = 8 };

typedef struct c4_net_poll_entry
{
	int64_t socket;
	int32_t wanted;   // kNetReadable | kNetWritable
	int32_t ready;    // what was found: any kNet* bits
} c4_net_poll_entry;

// Up to `capacity` addresses `host` (a name, or a numeric address) has, with `port`; how many, or -1.
int32_t c4_net_resolve(const char* host, int32_t port, c4_net_address* out, int32_t capacity, char* error, int64_t errorCapacity);

// "127.0.0.1:80", "[::1]:80" or "[fe80::1%3]:80": the text's length without the terminator, which is written, with the
// text, only when `capacity` has room for both.
int64_t c4_net_address_text(const c4_net_address* address, char* out, int64_t capacity);
// A numeric address with an optional port ("10.0.0.1", "10.0.0.1:80", "::1", "[::1]:443"); no name lookup. 0, or -1
// when the text is not one.
int32_t c4_net_parse_address(const char* text, c4_net_address* out);

// A TCP socket listening on `address` (port 0: a free port, which c4_net_local_address tells), or -1.
int64_t c4_net_tcp_listen(const c4_net_address* address, int32_t backlog, char* error, int64_t errorCapacity);
// The next connection to `listener` (its peer's address in `peer` when that is not null), kNetTimedOut, or -1.
int64_t c4_net_tcp_accept(int64_t listener, int32_t timeoutMs, c4_net_address* peer, char* error, int64_t errorCapacity);
// A TCP connection to `address`, kNetTimedOut, or -1 (refused, unreachable).
int64_t c4_net_tcp_connect(const c4_net_address* address, int32_t timeoutMs, char* error, int64_t errorCapacity);
// TCP_NODELAY: small writes go out at once instead of waiting to be combined. 0 or -1.
int32_t c4_net_set_no_delay(int64_t socket, int32_t on);

// A UDP socket bound to `address` (port 0: a free port), or -1.
int64_t c4_net_udp_open(const c4_net_address* address, char* error, int64_t errorCapacity);

// Bytes sent (at least one when size > 0; TCP may take fewer than `size`), kNetTimedOut, or -1. `to` is the
// destination of a UDP datagram, null for TCP. With a timeout a TCP send takes at most 64 KiB and gives up when they
// have not gone by then (kNetTimedOut; part of them may have).
int64_t c4_net_send(int64_t socket, const void* data, int64_t size, const c4_net_address* to, int32_t timeoutMs,
	char* error, int64_t errorCapacity);
// Bytes received, 0 when a TCP peer has closed its side, kNetTimedOut, or -1. `from` gets a UDP datagram's sender
// when it is not null.
int64_t c4_net_receive(int64_t socket, void* buffer, int64_t capacity, c4_net_address* from, int32_t timeoutMs,
	char* error, int64_t errorCapacity);

// The address a socket is bound to (with the port the system chose for port 0), or its peer's. 0 or -1.
int32_t c4_net_local_address(int64_t socket, c4_net_address* out);
int32_t c4_net_peer_address(int64_t socket, c4_net_address* out);

// Ends one direction or both; both also ends a send or receive another thread is waiting in.
void c4_net_shutdown(int64_t socket, int32_t how);
void c4_net_close(int64_t socket);

// Waits until an entry is ready for what it wants, or `timeoutMs` passes, and sets every entry's `ready`. How many
// are ready, 0 on a timeout, or -1.
int32_t c4_net_poll(c4_net_poll_entry* entries, int32_t count, int32_t timeoutMs);

#ifdef __cplusplus
}
#endif
