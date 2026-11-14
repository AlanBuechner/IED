// Checking a TLS server's certificate (TlsPosix.h) with OpenSSL's verifier. Client sessions share one context whose
// store holds the system's roots, loaded once: OpenSSL's default locations ($SSL_CERT_FILE and $SSL_CERT_DIR, else its
// OPENSSLDIR) and the first bundle found where the distributions keep theirs. The handshake verifies the server's
// chain against them without stopping on a failure (SSL_VERIFY_NONE; the verdict is read afterwards, as SChannel's
// manual validation is); when it fails for want of a trusted root and the caller gave extra roots, the chain is
// verified again with those alone, any CA certificate among them an anchor. Revocation is not checked: OpenSSL
// fetches no CRL or OCSP answer, so the status cannot be learned, which TlsRuntimeC.h lets pass.

#include "TlsPosix.h"
#include <arpa/inet.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

static const char kPemBegin[] = "-----BEGIN CERTIFICATE-----";
static const char kPemEnd[] = "-----END CERTIFICATE-----";

// Where the distributions keep the system's CA bundle.
static const char* const kSystemBundles[] = {
	"/etc/ssl/certs/ca-certificates.crt",                  // Debian, Ubuntu, TrueNAS SCALE, Arch, Gentoo
	"/etc/pki/tls/certs/ca-bundle.crt",                    // Fedora, RHEL
	"/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",   // RHEL, CentOS
	"/etc/ssl/ca-bundle.pem",                              // openSUSE
	"/etc/ssl/cert.pem",                                   // Alpine
};

// X509_V_ERR_* verdicts this file tells apart.
enum
{
	kVerdictUnspecified = 1,
	kVerdictNoIssuer = 2,
	kVerdictUnreadableSignature = 4,
	kVerdictBadSignature = 7,
	kVerdictNotYetValid = 9,
	kVerdictExpired = 10,
	kVerdictBadNotBefore = 13,
	kVerdictBadNotAfter = 14,
	kVerdictOutOfMemory = 17,
	kVerdictSelfSigned = 18,
	kVerdictSelfSignedInChain = 19,
	kVerdictNoLocalIssuer = 20,
	kVerdictLeafUnverifiable = 21,
	kVerdictRevoked = 23,
	kVerdictWrongPurpose = 26,
	kVerdictUntrusted = 27,
	kVerdictRejected = 28,
	kVerdictHostMismatch = 62,
	kVerdictAddressMismatch = 64,
};

// --- the system's roots ----------------------------------------------------------------------------------------------

static pthread_once_t g_clientOnce = PTHREAD_ONCE_INIT;
static SSL_CTX* g_clientContext;
static char g_clientProblem[kTlsReasonBytes];

static void LoadSystemRoots(const TlsApi* api, SSL_CTX* context)
{
	api->SSL_CTX_set_default_verify_paths(context);
	for (size_t i = 0; i < sizeof(kSystemBundles) / sizeof(kSystemBundles[0]); ++i)
		if (access(kSystemBundles[i], R_OK) == 0 && api->SSL_CTX_load_verify_locations(context, kSystemBundles[i], NULL) == 1) break;
	api->ERR_clear_error();   // a default location that is missing is no failure
}

static void MakeClientContext(void)
{
	const TlsApi* api = c4_tls_posix_api(g_clientProblem, sizeof(g_clientProblem));
	if (!api) return;
	api->ERR_clear_error();
	g_clientContext = c4_tls_posix_context(api, false);
	if (!g_clientContext)
	{
		c4_tls_posix_failed(api, "OpenSSL could not make a TLS client context", c4_tls_posix_take_error(api), g_clientProblem, sizeof(g_clientProblem));
		return;
	}
	LoadSystemRoots(api, g_clientContext);
}

SSL_CTX* c4_tls_posix_client_context(char* error, int64_t errorCapacity)
{
	pthread_once(&g_clientOnce, MakeClientContext);
	if (g_clientContext) return g_clientContext;
	c4_tls_posix_refused(g_clientProblem, error, errorCapacity);
	return NULL;
}

// --- the extra roots -------------------------------------------------------------------------------------------------

// Where `pattern` (`length` bytes) first starts in [from, end), or NULL.
static const char* Find(const char* from, const char* end, const char* pattern, size_t length)
{
	for (const char* at = from; at + length <= end; ++at)
		if (memcmp(at, pattern, length) == 0) return at;
	return NULL;
}

// The certificate in `certificate` (taken) added to `store`: whether it was.
static bool AddCertificate(const TlsApi* api, X509_STORE* store, X509* certificate)
{
	if (!certificate) return false;
	const bool added = api->X509_STORE_add_cert(store, certificate) == 1;
	api->X509_free(certificate);
	return added;
}

// The PEM block [begin, end) into `store`: whether it held a certificate.
static bool AddPemBlock(const TlsApi* api, X509_STORE* store, const char* begin, const char* end)
{
	BIO* block = api->BIO_new_mem_buf(begin, (int)(end - begin));
	if (!block) return false;
	const bool added = AddCertificate(api, store, api->PEM_read_bio_X509(block, NULL, NULL, NULL));
	api->BIO_free(block);
	return added;
}

// The certificates of every PEM CERTIFICATE block of `text`; -1 when it has no such block.
static int AddPem(const TlsApi* api, X509_STORE* store, const char* text, const char* end)
{
	const char* begin = Find(text, end, kPemBegin, sizeof(kPemBegin) - 1);
	if (!begin) return -1;
	int added = 0;
	while (begin)
	{
		const char* close = Find(begin, end, kPemEnd, sizeof(kPemEnd) - 1);
		if (!close) break;
		const char* blockEnd = close + sizeof(kPemEnd) - 1;
		if (AddPemBlock(api, store, begin, blockEnd)) ++added;
		begin = Find(blockEnd, end, kPemBegin, sizeof(kPemBegin) - 1);
	}
	return added;
}

static int AddDer(const TlsApi* api, X509_STORE* store, const void* bytes, int64_t size)
{
	const unsigned char* der = (const unsigned char*)bytes;
	return AddCertificate(api, store, api->d2i_X509(NULL, &der, (long)size)) ? 1 : 0;
}

bool c4_tls_posix_load_roots(const TlsApi* api, const void* bytes, int64_t size, X509_STORE** out, char* error, int64_t errorCapacity)
{
	if (size <= 0 || size > INT_MAX || !bytes)
	{
		c4_tls_posix_refused("the extra roots are empty or larger than 2 GiB", error, errorCapacity);
		return false;
	}
	X509_STORE* store = api->X509_STORE_new();
	if (!store)
	{
		c4_tls_posix_failed(api, "no certificate store could be made for the extra roots", c4_tls_posix_take_error(api), error, errorCapacity);
		return false;
	}
	const char* text = (const char*)bytes;
	int added = AddPem(api, store, text, text + size);
	if (added < 0) added = AddDer(api, store, bytes, size);
	api->ERR_clear_error();   // what a block that is no certificate left
	if (added == 0)
	{
		api->X509_STORE_free(store);
		c4_tls_posix_refused("the extra roots hold no certificate (PEM CERTIFICATE blocks, or one DER certificate)", error, errorCapacity);
		return false;
	}
	*out = store;
	return true;
}

// --- the host --------------------------------------------------------------------------------------------------------

bool c4_tls_posix_is_ip_address(const char* host)
{
	unsigned char address[16];
	return inet_pton(AF_INET, host, address) == 1 || inet_pton(AF_INET6, host, address) == 1;
}

bool c4_tls_posix_expect_host(const TlsApi* api, X509_VERIFY_PARAM* param, const char* host)
{
	if (c4_tls_posix_is_ip_address(host)) return api->X509_VERIFY_PARAM_set1_ip_asc(param, host) == 1;
	return api->X509_VERIFY_PARAM_set1_host(param, host, 0) == 1;
}

// --- the verdict -----------------------------------------------------------------------------------------------------

// Whether the verdict is for want of a trusted root, which the extra roots may make up for.
static bool WantsRoot(long verdict)
{
	return verdict == kVerdictNoIssuer || verdict == kVerdictNoLocalIssuer || verdict == kVerdictLeafUnverifiable ||
		verdict == kVerdictSelfSigned || verdict == kVerdictSelfSignedInChain || verdict == kVerdictUntrusted;
}

// The verdict on `chain` (the server's, its own certificate first) with only the extra roots trusted.
static long VerdictWithExtraRoots(const TlsApi* api, OPENSSL_STACK* chain, const TlsServerCheck* check)
{
	X509_STORE_CTX* context = api->X509_STORE_CTX_new();
	if (!context) return kVerdictOutOfMemory;
	long verdict = kVerdictUnspecified;
	X509* certificate = (X509*)api->OPENSSL_sk_value(chain, 0);
	if (api->X509_STORE_CTX_init(context, check->extraRoots, certificate, chain) == 1 &&
		api->X509_STORE_CTX_set_default(context, "ssl_server") == 1 &&
		c4_tls_posix_expect_host(api, api->X509_STORE_CTX_get0_param(context), check->host))
	{
		api->X509_VERIFY_PARAM_set_flags(api->X509_STORE_CTX_get0_param(context), kX509VerifyFlagPartialChain);
		const int verified = api->X509_verify_cert(context);
		const int found = api->X509_STORE_CTX_get_error(context);
		verdict = verified == 1 ? kX509Ok : found != kX509Ok ? found : kVerdictUnspecified;
	}
	api->X509_STORE_CTX_free(context);
	api->ERR_clear_error();
	return verdict;
}

static const char* VerdictText(long verdict)
{
	switch (verdict)
	{
	case kVerdictSelfSigned:
	case kVerdictSelfSignedInChain:
	case kVerdictUntrusted:
	case kVerdictRejected: return "it is not issued by an authority this machine trusts (an unknown or self-signed root)";
	case kVerdictNoIssuer:
	case kVerdictNoLocalIssuer:
	case kVerdictLeafUnverifiable: return "its chain does not reach a trusted root (an intermediate certificate is missing or unknown)";
	case kVerdictNotYetValid:
	case kVerdictExpired:
	case kVerdictBadNotBefore:
	case kVerdictBadNotAfter: return "it has expired, or is not valid yet";
	case kVerdictHostMismatch:
	case kVerdictAddressMismatch: return "it was issued for other names";
	case kVerdictRevoked: return "it has been revoked";
	case kVerdictWrongPurpose: return "it is not for server authentication";
	case kVerdictUnreadableSignature:
	case kVerdictBadSignature: return "its signature is not valid";
	default: return NULL;
	}
}

int64_t c4_tls_posix_check_server(const TlsApi* api, SSL* ssl, const TlsServerCheck* check, char* error, int64_t errorCapacity)
{
	char what[kTlsReasonBytes];
	snprintf(what, sizeof(what), "the certificate of %s is refused", check->host);
	OPENSSL_STACK* chain = api->SSL_get_peer_cert_chain(ssl);
	if (!chain || api->OPENSSL_sk_num(chain) <= 0) return c4_tls_posix_explained(what, "the server sent none", error, errorCapacity);
	long verdict = api->SSL_get_verify_result(ssl);
	if (WantsRoot(verdict) && check->extraRoots) verdict = VerdictWithExtraRoots(api, chain, check);
	if (verdict == kX509Ok) return c4_tls_posix_done(0, error, errorCapacity);
	const char* reason = VerdictText(verdict);
	if (reason) return c4_tls_posix_explained(what, reason, error, errorCapacity);
	char text[kTlsReasonBytes];
	snprintf(text, sizeof(text), "%s (verify error %ld)", api->X509_verify_cert_error_string(verdict), verdict);
	return c4_tls_posix_explained(what, text, error, errorCapacity);
}
