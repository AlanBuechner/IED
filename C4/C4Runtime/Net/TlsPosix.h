#pragma once
// What the OpenSSL files of TlsRuntimeC.h share: OpenSSL's functions (TlsApiPosix.c), the sessions and identities
// (TlsPosix.c, TlsIdentityPosix.c), the check of a server's certificate (TlsTrustPosix.c), and the results every call
// writes. OpenSSL is loaded at run time, never linked: Debian 11 and Ubuntu 20.04 ship only OpenSSL 1.1, Debian 12,
// Ubuntu 22.04 and TrueNAS SCALE only OpenSSL 3, and a program linked to one would not start on the other. So only
// functions both export with the same signature are used, declared here with OpenSSL's types opaque - none of
// OpenSSL's headers, which would tie the program to one version.

#include "TlsRuntimeC.h"
#include "NetRuntimeC.h"
#include "SystemErrorC.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { kTlsReasonBytes = 384 };

// --- OpenSSL's types and constants -----------------------------------------------------------------------------------

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_method_st SSL_METHOD;
typedef struct bio_st BIO;
typedef struct bio_method_st BIO_METHOD;
typedef struct x509_st X509;
typedef struct x509_store_st X509_STORE;
typedef struct x509_store_ctx_st X509_STORE_CTX;
typedef struct X509_VERIFY_PARAM_st X509_VERIFY_PARAM;
typedef struct evp_pkey_st EVP_PKEY;
typedef struct PKCS12_st PKCS12;
typedef struct stack_st OPENSSL_STACK;          // STACK_OF(X509): its functions take any stack
typedef struct ossl_init_settings_st OPENSSL_INIT_SETTINGS;
typedef struct ossl_provider_st OSSL_PROVIDER;
typedef struct ossl_lib_ctx_st OSSL_LIB_CTX;
typedef int (*TlsPemPasswordCallback)(char* buffer, int size, int writing, void* context);

// The values of OpenSSL's macros this runtime uses; the same in 1.1.1 and 3.x.
enum
{
	kSslErrorSsl = 1,
	kSslErrorWantRead = 2,
	kSslErrorSyscall = 5,
	kSslErrorZeroReturn = 6,
	kSslCtrlSetTlsextHostname = 55,               // SSL_set_tlsext_host_name
	kSslCtrlSetMinProtoVersion = 123,             // SSL_CTX_set_min_proto_version
	kTlsextNametypeHostName = 0,
	kTls12Version = 0x0303,
	kTls13Version = 0x0304,
	kX509Ok = 0,
	kX509VerifyFlagPartialChain = 0x80000,        // X509_V_FLAG_PARTIAL_CHAIN
	kSslAlertReasonOffset = 1000,                 // SSL_AD_REASON_OFFSET: a received alert's reason is 1000 + its number
};

static const uint64_t kSslOpNoTicket = 0x00004000;            // SSL_OP_NO_TICKET
static const uint64_t kSslOpNoRenegotiation = 0x40000000;     // SSL_OP_NO_RENEGOTIATION
static const uint64_t kOpensslInitFlags = 0x00000002 | 0x00200000 | 0x00080000;   // crypto and SSL error strings, no atexit

// OpenSSL's functions TLS calls, by result, name and parameters: each a member of TlsApi. SSL_CTX_set_options takes
// and returns unsigned long in 1.1 and uint64_t in 3: the same on x86-64 Linux.
#define C4_TLS_SSL_FUNCTIONS(F) \
	F(int, OPENSSL_init_ssl, (uint64_t options, const OPENSSL_INIT_SETTINGS* settings)) \
	F(const SSL_METHOD*, TLS_client_method, (void)) \
	F(const SSL_METHOD*, TLS_server_method, (void)) \
	F(SSL_CTX*, SSL_CTX_new, (const SSL_METHOD* method)) \
	F(void, SSL_CTX_free, (SSL_CTX* context)) \
	F(long, SSL_CTX_ctrl, (SSL_CTX* context, int command, long value, void* pointer)) \
	F(uint64_t, SSL_CTX_set_options, (SSL_CTX* context, uint64_t options)) \
	F(int, SSL_CTX_set_num_tickets, (SSL_CTX* context, size_t count)) \
	F(int, SSL_CTX_set_default_verify_paths, (SSL_CTX* context)) \
	F(int, SSL_CTX_load_verify_locations, (SSL_CTX* context, const char* file, const char* directory)) \
	F(int, SSL_CTX_use_cert_and_key, (SSL_CTX* context, X509* certificate, EVP_PKEY* key, OPENSSL_STACK* chain, int replace)) \
	F(SSL*, SSL_new, (SSL_CTX* context)) \
	F(void, SSL_free, (SSL* ssl)) \
	F(void, SSL_set_bio, (SSL* ssl, BIO* incoming, BIO* outgoing)) \
	F(void, SSL_set_connect_state, (SSL* ssl)) \
	F(void, SSL_set_accept_state, (SSL* ssl)) \
	F(int, SSL_do_handshake, (SSL* ssl)) \
	F(int, SSL_read, (SSL* ssl, void* buffer, int capacity)) \
	F(int, SSL_write, (SSL* ssl, const void* data, int size)) \
	F(int, SSL_shutdown, (SSL* ssl)) \
	F(int, SSL_get_error, (const SSL* ssl, int result)) \
	F(long, SSL_ctrl, (SSL* ssl, int command, long value, void* pointer)) \
	F(X509_VERIFY_PARAM*, SSL_get0_param, (SSL* ssl)) \
	F(long, SSL_get_verify_result, (const SSL* ssl)) \
	F(OPENSSL_STACK*, SSL_get_peer_cert_chain, (const SSL* ssl)) \
	F(int, SSL_version, (const SSL* ssl)) \
	F(int, SSL_has_pending, (const SSL* ssl))

#define C4_TLS_CRYPTO_FUNCTIONS(F) \
	F(unsigned long, ERR_get_error, (void)) \
	F(void, ERR_clear_error, (void)) \
	F(const char*, ERR_reason_error_string, (unsigned long code)) \
	F(BIO*, BIO_new, (const BIO_METHOD* method)) \
	F(const BIO_METHOD*, BIO_s_mem, (void)) \
	F(BIO*, BIO_new_mem_buf, (const void* bytes, int size)) \
	F(int, BIO_read, (BIO* bio, void* buffer, int capacity)) \
	F(int, BIO_write, (BIO* bio, const void* data, int size)) \
	F(size_t, BIO_ctrl_pending, (BIO* bio)) \
	F(int, BIO_free, (BIO* bio)) \
	F(X509*, PEM_read_bio_X509, (BIO* bio, X509** out, TlsPemPasswordCallback password, void* context)) \
	F(X509*, d2i_X509, (X509** out, const unsigned char** bytes, long size)) \
	F(void, X509_free, (X509* certificate)) \
	F(X509_STORE*, X509_STORE_new, (void)) \
	F(void, X509_STORE_free, (X509_STORE* store)) \
	F(int, X509_STORE_add_cert, (X509_STORE* store, X509* certificate)) \
	F(X509_STORE_CTX*, X509_STORE_CTX_new, (void)) \
	F(void, X509_STORE_CTX_free, (X509_STORE_CTX* context)) \
	F(int, X509_STORE_CTX_init, (X509_STORE_CTX* context, X509_STORE* store, X509* certificate, OPENSSL_STACK* untrusted)) \
	F(int, X509_STORE_CTX_set_default, (X509_STORE_CTX* context, const char* name)) \
	F(X509_VERIFY_PARAM*, X509_STORE_CTX_get0_param, (X509_STORE_CTX* context)) \
	F(int, X509_STORE_CTX_get_error, (X509_STORE_CTX* context)) \
	F(int, X509_verify_cert, (X509_STORE_CTX* context)) \
	F(const char*, X509_verify_cert_error_string, (long code)) \
	F(int, X509_VERIFY_PARAM_set1_host, (X509_VERIFY_PARAM* param, const char* name, size_t length)) \
	F(int, X509_VERIFY_PARAM_set1_ip_asc, (X509_VERIFY_PARAM* param, const char* address)) \
	F(int, X509_VERIFY_PARAM_set_flags, (X509_VERIFY_PARAM* param, unsigned long flags)) \
	F(int, OPENSSL_sk_num, (const OPENSSL_STACK* stack)) \
	F(void*, OPENSSL_sk_value, (const OPENSSL_STACK* stack, int index)) \
	F(void, OPENSSL_sk_pop_free, (OPENSSL_STACK* stack, void (*freeItem)(void*))) \
	F(PKCS12*, d2i_PKCS12, (PKCS12** out, const unsigned char** bytes, long size)) \
	F(void, PKCS12_free, (PKCS12* file)) \
	F(int, PKCS12_parse, (PKCS12* file, const char* password, EVP_PKEY** key, X509** certificate, OPENSSL_STACK** chain)) \
	F(int, PKCS12_verify_mac, (PKCS12* file, const char* password, int length)) \
	F(int, PKCS12_mac_present, (const PKCS12* file)) \
	F(void, EVP_PKEY_free, (EVP_PKEY* key))

#define C4_TLS_API_MEMBER(result, name, parameters) result (*name) parameters;

// OpenSSL's functions, loaded the first time TLS is used: a program that never uses it never loads OpenSSL.
typedef struct TlsApi
{
	C4_TLS_SSL_FUNCTIONS(C4_TLS_API_MEMBER)
	C4_TLS_CRYPTO_FUNCTIONS(C4_TLS_API_MEMBER)
	// OpenSSL 3's, NULL with 1.1: loads its legacy provider (RC2 and the other ciphers of old PKCS #12 files).
	OSSL_PROVIDER* (*OSSL_PROVIDER_try_load)(OSSL_LIB_CTX* library, const char* name, int keepFallbacks);
	bool openssl3;   // error codes pack the library and reason differently in 3
} TlsApi;

// The functions, loaded once; NULL with why when OpenSSL is not installed or lacks one.
const TlsApi* c4_tls_posix_api(char* error, int64_t errorCapacity);

typedef struct TlsIdentity
{
	const TlsApi* api;
	SSL_CTX* context;   // the server's: its certificate, key and chain
} TlsIdentity;

// A new context for a client or a server: TLS 1.2 and 1.3, no renegotiation, no session tickets; NULL when OpenSSL
// refuses (its error queued).
SSL_CTX* c4_tls_posix_context(const TlsApi* api, bool server);

// The context every client session is made from, with the system's roots: made the first time, then shared. NULL with
// why when it cannot be made.
SSL_CTX* c4_tls_posix_client_context(char* error, int64_t errorCapacity);

// The CA certificates of `bytes` (PEM CERTIFICATE blocks, or one DER certificate) in a new store; false with why.
bool c4_tls_posix_load_roots(const TlsApi* api, const void* bytes, int64_t size, X509_STORE** out, char* error, int64_t errorCapacity);

// Whether `host` is an IPv4 or IPv6 address: one the certificate must name as an address, and no SNI name.
bool c4_tls_posix_is_ip_address(const char* host);

// `host` as the name (or address) a certificate verified with `param` must carry; false when OpenSSL refuses it.
bool c4_tls_posix_expect_host(const TlsApi* api, X509_VERIFY_PARAM* param, const char* host);

// What a server's certificate is checked against: its name, and the extra roots (or NULL) trusted when the system's
// roots are not enough.
typedef struct TlsServerCheck
{
	const char* host;
	X509_STORE* extraRoots;
} TlsServerCheck;

// The peer's certificate of the handshake `ssl` just finished, checked as TlsRuntimeC.h's c4_tls_connect says. 0, or
// -1 with why.
int64_t c4_tls_posix_check_server(const TlsApi* api, SSL* ssl, const TlsServerCheck* check, char* error, int64_t errorCapacity);

// --- OpenSSL's errors ------------------------------------------------------------------------------------------------

// The first error OpenSSL queued on this thread, the rest dropped: 0 when none.
static inline unsigned long c4_tls_posix_take_error(const TlsApi* api)
{
	const unsigned long code = api->ERR_get_error();
	api->ERR_clear_error();
	return code;
}

// An error's library (ERR_LIB_SSL is 20) and reason: 1.1 packs them as 8 and 12 bits, 3 as 8 and 23 bits below a
// flag for errno values (ERR_SYSTEM_FLAG).
enum { kTlsLibrarySystem = 2, kTlsLibrarySsl = 20 };

static inline int c4_tls_posix_error_library(const TlsApi* api, unsigned long code)
{
	if (!api->openssl3) return (int)((code >> 24) & 0xFF);
	return (code & 0x80000000UL) ? kTlsLibrarySystem : (int)((code >> 23) & 0xFF);
}

static inline int c4_tls_posix_error_reason(const TlsApi* api, unsigned long code)
{
	return (int)(code & (api->openssl3 ? 0x7FFFFFUL : 0xFFFUL));
}

// OpenSSL's text for `code` ("wrong version number").
static inline void c4_tls_posix_error_text(const TlsApi* api, unsigned long code, char* out, size_t capacity)
{
	const char* reason = code ? api->ERR_reason_error_string(code) : NULL;
	if (reason) snprintf(out, capacity, "%s", reason);
	else if (code) snprintf(out, capacity, "OpenSSL error 0x%lX", code);
	else snprintf(out, capacity, "OpenSSL gave no reason");
}

// --- results ---------------------------------------------------------------------------------------------------------

static inline int64_t c4_tls_posix_refused(const char* why, char* error, int64_t errorCapacity)
{
	c4_copy_text(why, error, errorCapacity);
	return kNetError;
}

static inline int64_t c4_tls_posix_done(int64_t result, char* error, int64_t errorCapacity)
{
	c4_copy_text("", error, errorCapacity);
	return result;
}

// "<what>: <reason>", and kNetError.
static inline int64_t c4_tls_posix_explained(const char* what, const char* reason, char* error, int64_t errorCapacity)
{
	char text[kTlsReasonBytes * 2];
	snprintf(text, sizeof(text), "%s: %s", what, reason);
	return c4_tls_posix_refused(text, error, errorCapacity);
}

// "<what>: <OpenSSL's text for `code`>", and kNetError.
static inline int64_t c4_tls_posix_failed(const TlsApi* api, const char* what, unsigned long code, char* error, int64_t errorCapacity)
{
	char reason[kTlsReasonBytes];
	c4_tls_posix_error_text(api, code, reason, sizeof(reason));
	return c4_tls_posix_explained(what, reason, error, errorCapacity);
}
