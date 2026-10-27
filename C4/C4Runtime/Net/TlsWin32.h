#pragma once
// What the SChannel files of TlsRuntimeC.h share: the sessions and identities (TlsWin32.c, TlsIdentityWin32.c), the
// check of a server's certificate (TlsTrustWin32.c), and the results every call writes.

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
#include <subauth.h>
#include <schannel.h>
#include <security.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

enum { kTlsReasonBytes = 384 };

typedef struct TlsIdentity
{
	CredHandle credentials;
	HCERTSTORE store;
	PCCERT_CONTEXT certificate;
} TlsIdentity;

// SChannel's credentials for a client (no certificate; it checks the server itself) or a server (`certificate`, with
// its key): TLS 1.2 and 1.3 where the system has SCH_CREDENTIALS, else TLS 1.2.
SECURITY_STATUS c4_tls_win32_credentials(bool server, PCCERT_CONTEXT* certificate, CredHandle* out);

// The CA certificates of `bytes` (PEM CERTIFICATE blocks, or one DER certificate) in a new memory store; false with why.
bool c4_tls_win32_load_roots(const void* bytes, int64_t size, HCERTSTORE* out, char* error, int64_t errorCapacity);

// The peer's certificate of `context`, checked as TlsRuntimeC.h's c4_tls_connect says, for the server `host`
// (`hostText` in messages), with `extraRoots` (or NULL) trusted when the system's roots are not enough; revocation
// downloads wait at most `urlWaitMs` in all (0: the system's own waits). 0, or -1 with why.
int64_t c4_tls_win32_check_server(CtxtHandle* context, const wchar_t* host, const char* hostText, HCERTSTORE extraRoots, DWORD urlWaitMs,
	char* error, int64_t errorCapacity);

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
