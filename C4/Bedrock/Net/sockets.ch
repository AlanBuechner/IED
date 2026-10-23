// The runtime's sockets (C4Runtime/NetRuntimeC.h), for Bedrock's Net files only.

include "address.ch"
include "poll.ch"

[link("c4_net_tcp_listen")] int64 c4_net_tcp_listen(const Net::Address* address, int32 backlog, char* error, int64 errorCapacity) noexcept;
[link("c4_net_tcp_accept")] int64 c4_net_tcp_accept(int64 listener, int32 timeoutMs, Net::Address* peer, char* error, int64 errorCapacity) noexcept;
[link("c4_net_tcp_connect")] int64 c4_net_tcp_connect(const Net::Address* address, int32 timeoutMs, char* error, int64 errorCapacity) noexcept;
[link("c4_net_set_no_delay")] int32 c4_net_set_no_delay(int64 socket, int32 on) noexcept;
[link("c4_net_udp_open")] int64 c4_net_udp_open(const Net::Address* address, char* error, int64 errorCapacity) noexcept;
[link("c4_net_send")] int64 c4_net_send(int64 socket, const void* data, int64 size, const Net::Address* to, int32 timeoutMs,
	char* error, int64 errorCapacity) noexcept;
[link("c4_net_receive")] int64 c4_net_receive(int64 socket, void* buffer, int64 capacity, Net::Address* from, int32 timeoutMs,
	char* error, int64 errorCapacity) noexcept;
[link("c4_net_local_address")] int32 c4_net_local_address(int64 socket, Net::Address* out) noexcept;
[link("c4_net_peer_address")] int32 c4_net_peer_address(int64 socket, Net::Address* out) noexcept;
[link("c4_net_shutdown")] void c4_net_shutdown(int64 socket, int32 how) noexcept;
[link("c4_net_close")] void c4_net_close(int64 socket) noexcept;
[link("c4_net_poll")] int32 c4_net_poll(Net::PollEntry* entries, int32 count, int32 timeoutMs) noexcept;

namespace Net
{
	// The address a socket is bound to, or its peer's; not valid when the socket has none.
	Address LocalAddressOf(int64 socket) {
		Address address;
		if (socket >= 0) c4_net_local_address(socket, &address);
		return address;
	}

	Address PeerAddressOf(int64 socket) {
		Address address;
		if (socket >= 0) c4_net_peer_address(socket, &address);
		return address;
	}
}
