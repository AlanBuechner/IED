// The runtime's TLS (C4Runtime/TlsRuntimeC.h), for Bedrock's Net files only.

include "tls.ch"

namespace Net
{
	// The runtime's c4_tls_client_options.
	struct TlsClientRecord
	{
		public const char* hostName;
		public const void* extraRoots;
		public int64 extraRootsSize;
	}
}

[link("c4_tls_connect")] int64 c4_tls_connect(int64 socket, const Net::TlsClientRecord* options, int32 timeoutMs, char* error, int64 errorCapacity) noexcept;
[link("c4_tls_identity_load")] int64 c4_tls_identity_load(const void* pkcs12, int64 size, const char* password, char* error, int64 errorCapacity) noexcept;
[link("c4_tls_identity_free")] void c4_tls_identity_free(int64 identity) noexcept;
[link("c4_tls_accept")] int64 c4_tls_accept(int64 socket, int64 identity, int32 timeoutMs, char* error, int64 errorCapacity) noexcept;
[link("c4_tls_send")] int64 c4_tls_send(int64 session, const void* data, int64 size, int32 timeoutMs, char* error, int64 errorCapacity) noexcept;
[link("c4_tls_receive")] int64 c4_tls_receive(int64 session, void* buffer, int64 capacity, int32 timeoutMs, char* error, int64 errorCapacity) noexcept;
[link("c4_tls_close_notify")] int32 c4_tls_close_notify(int64 session) noexcept;
[link("c4_tls_version")] int32 c4_tls_version(int64 session) noexcept;
[link("c4_tls_free")] void c4_tls_free(int64 session) noexcept;
