// The system's TLS and certificate functions (TlsWin32.h's TlsApi), loaded from the system folder the first time TLS
// is used and kept for the process, so a program that never uses TLS imports none of secur32, crypt32 and ncrypt.

#include "TlsWin32.h"

static INIT_ONCE g_tlsApiOnce = INIT_ONCE_STATIC_INIT;
static TlsApi g_tlsApi;
static char g_tlsApiProblem[kTlsReasonBytes];

// `name` of `module` into `*out`; false with the problem written when the DLL or the function is not there.
static bool Resolve(HMODULE module, const char* dll, const char* name, void** out)
{
	*out = module ? (void*)GetProcAddress(module, name) : NULL;
	if (!*out) snprintf(g_tlsApiProblem, sizeof(g_tlsApiProblem), "TLS is not available: %s has no %s", dll, name);
	return *out != NULL;
}

static HMODULE SystemLibrary(const wchar_t* name) { return LoadLibraryExW(name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32); }

static BOOL CALLBACK LoadApi(PINIT_ONCE once, PVOID parameter, PVOID* context)
{
	(void)once; (void)parameter; (void)context;
	const HMODULE secur32 = SystemLibrary(L"secur32.dll");
	const HMODULE crypt32 = SystemLibrary(L"crypt32.dll");
	const HMODULE ncrypt = SystemLibrary(L"ncrypt.dll");
	INIT_SECURITY_INTERFACE_W initialize = NULL;
	bool ok = Resolve(secur32, "secur32.dll", "InitSecurityInterfaceW", (void**)&initialize);
#define C4_TLS_FROM_CRYPT32(name) ok = ok && Resolve(crypt32, "crypt32.dll", #name, (void**)&g_tlsApi.name);
#define C4_TLS_FROM_NCRYPT(name) ok = ok && Resolve(ncrypt, "ncrypt.dll", #name, (void**)&g_tlsApi.name);
	C4_TLS_CRYPT32_FUNCTIONS(C4_TLS_FROM_CRYPT32)
	C4_TLS_NCRYPT_FUNCTIONS(C4_TLS_FROM_NCRYPT)
#undef C4_TLS_FROM_CRYPT32
#undef C4_TLS_FROM_NCRYPT
	g_tlsApi.sspi = ok ? initialize() : NULL;
	if (ok && !g_tlsApi.sspi) snprintf(g_tlsApiProblem, sizeof(g_tlsApiProblem), "TLS is not available: secur32.dll gave no SSPI functions");
	return TRUE;
}

const TlsApi* c4_tls_win32_api(char* error, int64_t errorCapacity)
{
	InitOnceExecuteOnce(&g_tlsApiOnce, LoadApi, NULL, NULL);
	if (g_tlsApi.sspi) return &g_tlsApi;
	c4_tls_win32_refused(g_tlsApiProblem, error, errorCapacity);
	return NULL;
}
