// OpenSSL's functions (TlsPosix.h's TlsApi), loaded the first time TLS is used and kept for the process: OpenSSL 3
// where the system has it, else 1.1, each library with its own libcrypto. A system without either fails TLS calls with
// why; the program itself still starts.

#include "TlsPosix.h"
#include <dlfcn.h>
#include <pthread.h>

// The libraries' sonames, newest first.
typedef struct OpenSslRelease
{
	const char* crypto;
	const char* ssl;
	bool openssl3;
} OpenSslRelease;

static const OpenSslRelease kReleases[] = {
	{ "libcrypto.so.3", "libssl.so.3", true },
	{ "libcrypto.so.1.1", "libssl.so.1.1", false },
};

static pthread_once_t g_tlsApiOnce = PTHREAD_ONCE_INIT;
static TlsApi g_tlsApi;
static bool g_tlsApiLoaded;
static char g_tlsApiProblem[kTlsReasonBytes];

// `name` of `library` into `*out`; false with the problem written when the library lacks it.
static bool Resolve(void* library, const char* file, const char* name, void** out)
{
	*out = dlsym(library, name);
	if (!*out) snprintf(g_tlsApiProblem, sizeof(g_tlsApiProblem), "TLS is not available: %s has no %s", file, name);
	return *out != NULL;
}

// The first release whose two libraries load: true with them open, else false with why.
static bool OpenRelease(const OpenSslRelease** release, void** crypto, void** ssl)
{
	char lastProblem[kTlsReasonBytes] = "";
	for (size_t i = 0; i < sizeof(kReleases) / sizeof(kReleases[0]); ++i)
	{
		*crypto = dlopen(kReleases[i].crypto, RTLD_NOW | RTLD_LOCAL);
		*ssl = *crypto ? dlopen(kReleases[i].ssl, RTLD_NOW | RTLD_LOCAL) : NULL;
		if (*ssl)
		{
			*release = &kReleases[i];
			return true;
		}
		const char* problem = dlerror();
		snprintf(lastProblem, sizeof(lastProblem), "%s", problem ? problem : kReleases[i].ssl);
		if (*crypto) dlclose(*crypto);
	}
	snprintf(g_tlsApiProblem, sizeof(g_tlsApiProblem),
		"TLS is not available: OpenSSL 3 (libssl.so.3) or 1.1 (libssl.so.1.1) is needed, and neither loads (%s)", lastProblem);
	return false;
}

static void LoadApi(void)
{
	const OpenSslRelease* release = NULL;
	void* crypto = NULL;
	void* ssl = NULL;
	if (!OpenRelease(&release, &crypto, &ssl)) return;
	bool ok = true;
#define C4_TLS_FROM_SSL(result, name, parameters) ok = ok && Resolve(ssl, release->ssl, #name, (void**)&g_tlsApi.name);
#define C4_TLS_FROM_CRYPTO(result, name, parameters) ok = ok && Resolve(crypto, release->crypto, #name, (void**)&g_tlsApi.name);
	C4_TLS_SSL_FUNCTIONS(C4_TLS_FROM_SSL)
	C4_TLS_CRYPTO_FUNCTIONS(C4_TLS_FROM_CRYPTO)
#undef C4_TLS_FROM_SSL
#undef C4_TLS_FROM_CRYPTO
	*(void**)&g_tlsApi.OSSL_PROVIDER_try_load = release->openssl3 ? dlsym(crypto, "OSSL_PROVIDER_try_load") : NULL;
	g_tlsApi.openssl3 = release->openssl3;
	// The error strings now, so every failure has its text; and no exit handler, which would free OpenSSL's state
	// under a thread still in a TLS call while the process ends.
	if (ok && g_tlsApi.OPENSSL_init_ssl(kOpensslInitFlags, NULL) != 1)
	{
		snprintf(g_tlsApiProblem, sizeof(g_tlsApiProblem), "TLS is not available: %s failed to start", release->ssl);
		ok = false;
	}
	g_tlsApiLoaded = ok;
}

const TlsApi* c4_tls_posix_api(char* error, int64_t errorCapacity)
{
	pthread_once(&g_tlsApiOnce, LoadApi);
	if (g_tlsApiLoaded) return &g_tlsApi;
	c4_tls_posix_refused(g_tlsApiProblem, error, errorCapacity);
	return NULL;
}
