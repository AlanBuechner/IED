#pragma once
// TLS behind Bedrock's Net::TcpStream (D-84): a connected TCP socket of NetRuntimeC.h secured with TLS 1.2 or 1.3, as
// the client (c4_tls_connect: the server's certificate checked against the system's roots, or extra ones the caller
// trusts, and against the host name) or as the server (c4_tls_accept, proving itself with an identity loaded from a
// PKCS #12 file). One set of implementation files per OS (Net/Tls*Win32.c: SChannel, its DLLs loaded when TLS is
// first used). A session or an identity is an int64 handle, -1 for none. A session sends and receives on its socket;
// the socket stays the caller's to shut down and close, after c4_tls_free. A call that can fail writes why into
// `error` as NetRuntimeC.h's calls do.
//
// Threads: one send and one receive may run at once on a session (each kind is serialized); c4_tls_free runs alone,
// once no other call is in it (a socket shut down both ways ends a call that waits).

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// What a client checks the server against.
typedef struct c4_tls_client_options
{
	const char* hostName;      // UTF-8: sent as SNI, and the name (or IP address) the certificate must carry
	const void* extraRoots;    // CA certificates trusted besides the system's roots: PEM (any number) or one DER; or null
	int64_t extraRootsSize;
} c4_tls_client_options;

// What c4_tls_version answers.
enum { kTlsVersionNone = 0, kTlsVersion12 = 12, kTlsVersion13 = 13 };

// The client's session on connected `socket`, once the handshake is done and the server's certificate checked:
// it chains to a root the system trusts, or to one of the extra roots; it is in its validity period, for server
// authentication, not revoked (a revocation status that cannot be learned does not refuse it), and names
// `hostName`. kNetTimedOut when the handshake takes longer than `timeoutMs`, or -1 with why.
int64_t c4_tls_connect(int64_t socket, const c4_tls_client_options* options, int32_t timeoutMs, char* error, int64_t errorCapacity);

// A server's certificate and private key from a PKCS #12 (.pfx) file's bytes and its password (UTF-8, "" for none),
// or -1 with why. While it is loaded the key is in the user's key store under a name of its own (SChannel reads keys
// from there); c4_tls_identity_free deletes it.
int64_t c4_tls_identity_load(const void* pkcs12, int64_t size, const char* password, char* error, int64_t errorCapacity);
void c4_tls_identity_free(int64_t identity);

// The server's session on accepted `socket`, proving itself with `identity` (which must outlive the session),
// kNetTimedOut, or -1 with why.
int64_t c4_tls_accept(int64_t socket, int64_t identity, int32_t timeoutMs, char* error, int64_t errorCapacity);

// Bytes of `data` sent, as one TLS record (at most the record size; at least one byte when size > 0), kNetTimedOut
// when the socket took nothing within `timeoutMs` (nothing was sent), or -1. A record cut off by a failing or late
// piece breaks the session: every later send fails.
int64_t c4_tls_send(int64_t session, const void* data, int64_t size, int32_t timeoutMs, char* error, int64_t errorCapacity);
// Decrypted bytes received, 0 once the peer has closed (close_notify, or the connection's end between records),
// kNetTimedOut, or -1.
int64_t c4_tls_receive(int64_t session, void* buffer, int64_t capacity, int32_t timeoutMs, char* error, int64_t errorCapacity);
// Sends close_notify, after which the peer's receive returns 0 and this side sends no more; only when no send is in
// progress and the socket takes it at once. 0, or -1 when it was not sent.
int32_t c4_tls_close_notify(int64_t session);
// kTlsVersion12 or kTlsVersion13; kTlsVersionNone for no session.
int32_t c4_tls_version(int64_t session);
void c4_tls_free(int64_t session);

#ifdef __cplusplus
}
#endif
