#pragma once
// What the SChannel files of TlsRuntimeC.h share: the system's functions (TlsApiWin32.c), the sessions and identities
// (TlsWin32.c, TlsIdentityWin32.c), the check of a server's certificate (TlsTrustWin32.c), and the results every call
// writes.

#include "TlsRuntimeC.h"
#include "NetRuntimeC.h"
#include "SystemErrorC.h"
#define WIN32_LEAN_AND_MEAN
#define SECURITY_WIN32
#define SCHANNEL_USE_BLACKLISTS               // SCH_CREDENTIALS and TLS_PARAMETERS
#define CERT_CHAIN_PARA_HAS_EXTRA_FIELDS      // CERT_CHAIN_PARA.dwUrlRetrievalTimeout
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#include <subauth.h>
#include <schannel.h>
#include <security.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum { kTlsReasonBytes = 384 };

// The functions of crypt32.dll and ncrypt.dll TLS calls, by name: each a member of TlsApi.
#define C4_TLS_CRYPT32_FUNCTIONS(F) F(CertOpenStore) F(CertCloseStore) F(CertAddEncodedCertificateToStore) F(CryptStringToBinaryA) \
	F(CertGetCertificateChain) F(CertFreeCertificateChain) F(CertVerifyCertificateChainPolicy) F(CertCreateCertificateChainEngine) \
	F(CertFreeCertificateChainEngine) F(CertFreeCertificateContext) F(CertFindCertificateInStore) F(CertEnumCertificatesInStore) \
	F(CertGetCertificateContextProperty) F(PFXIsPFXBlob) F(PFXImportCertStore)
#define C4_TLS_NCRYPT_FUNCTIONS(F) F(NCryptOpenStorageProvider) F(NCryptOpenKey) F(NCryptDeleteKey) F(NCryptFreeObject)
#define C4_TLS_API_MEMBER(name) __typeof__(&name) name;

// The system's TLS (SSPI's function table from secur32.dll) and certificate functions, loaded the first time TLS is
// used: a program that never uses it never loads secur32, crypt32 or ncrypt - each would otherwise load at its start.
typedef struct TlsApi
{
	PSecurityFunctionTableW sspi;
	C4_TLS_CRYPT32_FUNCTIONS(C4_TLS_API_MEMBER)
	C4_TLS_NCRYPT_FUNCTIONS(C4_TLS_API_MEMBER)
} TlsApi;

// The functions, loaded once; NULL with why when the system lacks one.
const TlsApi* c4_tls_win32_api(char* error, int64_t errorCapacity);

typedef struct TlsIdentity
{
	const TlsApi* api;
	CredHandle credentials;
	HCERTSTORE store;
	PCCERT_CONTEXT certificate;
} TlsIdentity;

// SChannel's credentials for a client (no certificate; it checks the server itself) or a server (`certificate`, with
// its key): TLS 1.2 and 1.3 where the system has SCH_CREDENTIALS, else TLS 1.2.
SECURITY_STATUS c4_tls_win32_credentials(const TlsApi* api, bool server, PCCERT_CONTEXT* certificate, CredHandle* out);

// The CA certificates of `bytes` (PEM CERTIFICATE blocks, or one DER certificate) in a new memory store; false with why.
bool c4_tls_win32_load_roots(const TlsApi* api, const void* bytes, int64_t size, HCERTSTORE* out, char* error, int64_t errorCapacity);

// What a server's certificate is checked against: its name (`host`; `hostText` in messages), the extra roots (or
// NULL) trusted when the system's roots are not enough, and how long revocation downloads may take in all (0: the
// system's own waits).
typedef struct TlsServerCheck
{
	const wchar_t* host;
	const char* hostText;
	HCERTSTORE extraRoots;
	DWORD urlWaitMs;
} TlsServerCheck;

// The peer's certificate of `context`, checked as TlsRuntimeC.h's c4_tls_connect says. 0, or -1 with why.
int64_t c4_tls_win32_check_server(const TlsApi* api, CtxtHandle* context, const TlsServerCheck* check, char* error, int64_t errorCapacity);

static inline int64_t c4_tls_win32_refused(const char* why, char* error, int64_t errorCapacity)
{
	c4_copy_text(why, error, errorCapacity);
	return kNetError;
}

static inline int64_t c4_tls_win32_done(int64_t result, char* error, int64_t errorCapacity)
{
	c4_copy_text("", error, errorCapacity);
	return result;
}

// "<what>: <the system's message> (0x80090326)", and kNetError.
static inline int64_t c4_tls_win32_failed(const char* what, SECURITY_STATUS status, char* error, int64_t errorCapacity)
{
	char message[kTlsReasonBytes];
	c4_system_error_text((uint32_t)status, message, sizeof(message));
	char* code = strstr(message, " (error ");
	if (code) *code = 0;
	char text[kTlsReasonBytes * 2];
	snprintf(text, sizeof(text), "%s: %s (0x%08lX)", what, message, (unsigned long)status);
	return c4_tls_win32_refused(text, error, errorCapacity);
}

// "<what>: <reason>", and kNetError.
static inline int64_t c4_tls_win32_explained(const char* what, const char* reason, char* error, int64_t errorCapacity)
{
	char text[kTlsReasonBytes * 2];
	snprintf(text, sizeof(text), "%s: %s", what, reason);
	return c4_tls_win32_refused(text, error, errorCapacity);
}
