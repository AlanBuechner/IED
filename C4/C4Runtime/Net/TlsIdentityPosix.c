// OpenSSL contexts (TlsPosix.h) and a TLS server's identity (TlsRuntimeC.h): a certificate, its private key and its
// chain from PKCS #12 bytes, in a server context of the identity's own. OpenSSL keeps the key in this process, so
// nothing is stored anywhere and releasing the identity only frees it. OpenSSL 3 decrypts a file made with the older
// ciphers (RC2: older tools' and Windows' default) only through its legacy provider, loaded when a file needs it.

#include "TlsPosix.h"
#include <limits.h>
#include <stdlib.h>

SSL_CTX* c4_tls_posix_context(const TlsApi* api, bool server)
{
	SSL_CTX* context = api->SSL_CTX_new(server ? api->TLS_server_method() : api->TLS_client_method());
	if (!context) return NULL;
	if (api->SSL_CTX_ctrl(context, kSslCtrlSetMinProtoVersion, kTls12Version, NULL) != 1)
	{
		api->SSL_CTX_free(context);
		return NULL;
	}
	// No renegotiation: a send would have to wait for its handshake on the receiving thread (a peer asking for one is
	// told no; TLS 1.3 has none). No session tickets: a session is never resumed.
	api->SSL_CTX_set_options(context, kSslOpNoRenegotiation | kSslOpNoTicket);
	if (server) api->SSL_CTX_set_num_tickets(context, 0);
	return context;
}

static void FreeIdentity(TlsIdentity* id)
{
	if (id->context) id->api->SSL_CTX_free(id->context);
	free(id);
}

static int64_t Abandon(TlsIdentity* id, int64_t result)
{
	FreeIdentity(id);
	return result;
}

// What PKCS12_parse gives, freed together.
typedef struct Pkcs12Contents
{
	EVP_PKEY* key;
	X509* certificate;
	OPENSSL_STACK* chain;
} Pkcs12Contents;

static void FreeContents(const TlsApi* api, Pkcs12Contents* contents)
{
	if (contents->key) api->EVP_PKEY_free(contents->key);
	if (contents->certificate) api->X509_free(contents->certificate);
	if (contents->chain) api->OPENSSL_sk_pop_free(contents->chain, (void (*)(void*))api->X509_free);
}

// Whether `password` opens the file's integrity check (a file without one opens with any). An empty password may have
// been taken as none when the file was made.
static bool PasswordOpens(const TlsApi* api, PKCS12* file, const char* password)
{
	if (!api->PKCS12_mac_present(file) || api->PKCS12_verify_mac(file, password, -1) == 1) return true;
	return !password[0] && api->PKCS12_verify_mac(file, NULL, 0) == 1;
}

// The file's key, certificate and chain; once more with OpenSSL 3's legacy provider when the first try fails.
static bool Parse(const TlsApi* api, PKCS12* file, const char* password, Pkcs12Contents* out, unsigned long* problem)
{
	api->ERR_clear_error();
	if (api->PKCS12_parse(file, password, &out->key, &out->certificate, &out->chain) == 1) return true;
	*problem = c4_tls_posix_take_error(api);
	if (!api->OSSL_PROVIDER_try_load || !api->OSSL_PROVIDER_try_load(NULL, "legacy", 1)) return false;
	FreeContents(api, out);
	memset(out, 0, sizeof(*out));
	if (api->PKCS12_parse(file, password, &out->key, &out->certificate, &out->chain) == 1) return true;
	api->ERR_clear_error();
	return false;
}

// The identity's context, with the file's certificate, key and chain; 0, or -1 with why.
static int64_t UseContents(TlsIdentity* id, Pkcs12Contents* contents, char* error, int64_t errorCapacity)
{
	if (!contents->key || !contents->certificate)
		return c4_tls_posix_refused("the PKCS #12 file holds no certificate with its private key", error, errorCapacity);
	id->context = c4_tls_posix_context(id->api, true);
	if (!id->context) return c4_tls_posix_failed(id->api, "OpenSSL could not make a TLS server context", c4_tls_posix_take_error(id->api), error, errorCapacity);
	if (id->api->SSL_CTX_use_cert_and_key(id->context, contents->certificate, contents->key, contents->chain, 1) == 1) return 0;
	return c4_tls_posix_failed(id->api, "OpenSSL refused the TLS identity", c4_tls_posix_take_error(id->api), error, errorCapacity);
}

// The identity's certificate, key and context; 0, or -1 with why.
static int64_t LoadIdentity(TlsIdentity* id, const void* pkcs12, int64_t size, const char* password, char* error, int64_t errorCapacity)
{
	const TlsApi* api = id->api;
	const unsigned char* bytes = (const unsigned char*)pkcs12;
	PKCS12* file = api->d2i_PKCS12(NULL, &bytes, (long)size);
	if (!file)
	{
		api->ERR_clear_error();
		return c4_tls_posix_refused("not a PKCS #12 (.pfx) file", error, errorCapacity);
	}
	Pkcs12Contents contents = { NULL, NULL, NULL };
	unsigned long problem = 0;
	int64_t result = 0;
	if (!PasswordOpens(api, file, password)) result = c4_tls_posix_refused("the password does not open the PKCS #12 file", error, errorCapacity);
	else if (!Parse(api, file, password, &contents, &problem)) result = c4_tls_posix_failed(api, "the PKCS #12 file cannot be read", problem, error, errorCapacity);
	else result = UseContents(id, &contents, error, errorCapacity);
	FreeContents(api, &contents);
	api->PKCS12_free(file);
	api->ERR_clear_error();
	return result;
}

int64_t c4_tls_identity_load(const void* pkcs12, int64_t size, const char* password, char* error, int64_t errorCapacity)
{
	if (!pkcs12 || size <= 0 || size > INT_MAX) return c4_tls_posix_refused("no PKCS #12 bytes", error, errorCapacity);
	const TlsApi* api = c4_tls_posix_api(error, errorCapacity);
	if (!api) return kNetError;
	TlsIdentity* id = (TlsIdentity*)calloc(1, sizeof(TlsIdentity));
	if (!id) return c4_tls_posix_refused("out of memory for a TLS identity", error, errorCapacity);
	id->api = api;
	const int64_t loaded = LoadIdentity(id, pkcs12, size, password ? password : "", error, errorCapacity);
	if (loaded != 0) return Abandon(id, loaded);
	return c4_tls_posix_done((int64_t)(intptr_t)id, error, errorCapacity);
}

void c4_tls_identity_free(int64_t identity)
{
	if (identity != -1 && identity != 0) FreeIdentity((TlsIdentity*)(intptr_t)identity);
}
