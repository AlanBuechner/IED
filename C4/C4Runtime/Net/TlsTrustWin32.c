// Checking a TLS server's certificate (TlsWin32.h) with CryptoAPI: the chain to the system's roots, or - when that
// fails for want of a trusted root and the caller gave extra roots - to those alone, through a chain engine whose
// exclusive roots they are; then the SSL policy with the host name. Revocation is checked below the root: a status
// that cannot be learned (no CRL, offline) does not refuse, a revoked certificate does.

#include "TlsWin32.h"
#include <limits.h>
#include <stdlib.h>

static const char kPemBegin[] = "-----BEGIN CERTIFICATE-----";
static const char kPemEnd[] = "-----END CERTIFICATE-----";

// --- the extra roots -------------------------------------------------------------------------------------------------

// Where `pattern` (`length` bytes) first starts in [from, end), or NULL.
static const char* Find(const char* from, const char* end, const char* pattern, size_t length)
{
	for (const char* at = from; at + length <= end; ++at)
		if (memcmp(at, pattern, length) == 0) return at;
	return NULL;
}

// The PEM block [begin, end) into `store`: whether it held a certificate.
static bool AddPemBlock(const TlsApi* api, HCERTSTORE store, const char* begin, const char* end)
{
	const DWORD length = (DWORD)(end - begin);
	DWORD derSize = 0;
	if (!api->CryptStringToBinaryA(begin, length, CRYPT_STRING_BASE64HEADER, NULL, &derSize, NULL, NULL)) return false;
	BYTE* der = (BYTE*)malloc(derSize);
	const bool added = der && api->CryptStringToBinaryA(begin, length, CRYPT_STRING_BASE64HEADER, der, &derSize, NULL, NULL) &&
		api->CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, der, derSize, CERT_STORE_ADD_ALWAYS, NULL);
	free(der);
	return added;
}

// The certificates of every PEM CERTIFICATE block of `text`; -1 when it has no such block.
static int AddPem(const TlsApi* api, HCERTSTORE store, const char* text, const char* end)
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

bool c4_tls_win32_load_roots(const TlsApi* api, const void* bytes, int64_t size, HCERTSTORE* out, char* error, int64_t errorCapacity)
{
	if (size <= 0 || size > INT_MAX || !bytes)
	{
		c4_tls_win32_refused("the extra roots are empty or larger than 2 GiB", error, errorCapacity);
		return false;
	}
	HCERTSTORE store = api->CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, NULL);
	if (!store)
	{
		c4_tls_win32_failed("no certificate store could be made for the extra roots", (SECURITY_STATUS)GetLastError(), error, errorCapacity);
		return false;
	}
	const char* text = (const char*)bytes;
	int added = AddPem(api, store, text, text + size);
	if (added < 0) added = api->CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, (const BYTE*)bytes, (DWORD)size, CERT_STORE_ADD_ALWAYS, NULL) ? 1 : 0;
	if (added == 0)
	{
		api->CertCloseStore(store, 0);
		c4_tls_win32_refused("the extra roots hold no certificate (PEM CERTIFICATE blocks, or one DER certificate)", error, errorCapacity);
		return false;
	}
	*out = store;
	return true;
}

// --- the chain -------------------------------------------------------------------------------------------------------

// The chain from `certificate` up to a root `engine` trusts (NULL: the system's).
static PCCERT_CHAIN_CONTEXT BuildChain(const TlsApi* api, HCERTCHAINENGINE engine, PCCERT_CONTEXT certificate, DWORD urlWaitMs)
{
	LPSTR usages[1] = { (LPSTR)szOID_PKIX_KP_SERVER_AUTH };
	CERT_CHAIN_PARA para;
	memset(&para, 0, sizeof(para));
	para.cbSize = sizeof(para);
	para.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
	para.RequestedUsage.Usage.cUsageIdentifier = 1;
	para.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
	para.dwUrlRetrievalTimeout = urlWaitMs;
	const DWORD flags = CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT | (urlWaitMs ? CERT_CHAIN_REVOCATION_ACCUMULATIVE_TIMEOUT : 0);
	PCCERT_CHAIN_CONTEXT chain = NULL;
	return api->CertGetCertificateChain(engine, certificate, NULL, certificate->hCertStore, &para, flags, NULL, &chain) ? chain : NULL;
}

// The SSL policy's verdict on the chain `engine` builds for the server: 0, or a CERT_E_* / CRYPT_E_* code.
static DWORD Verdict(const TlsApi* api, HCERTCHAINENGINE engine, PCCERT_CONTEXT certificate, const TlsServerCheck* check)
{
	PCCERT_CHAIN_CONTEXT chain = BuildChain(api, engine, certificate, check->urlWaitMs);
	if (!chain) return (DWORD)CERT_E_CHAINING;
	SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl;
	memset(&ssl, 0, sizeof(ssl));
	ssl.cbSize = sizeof(ssl);
	ssl.dwAuthType = AUTHTYPE_SERVER;
	ssl.pwszServerName = (wchar_t*)check->host;
	CERT_CHAIN_POLICY_PARA policy;
	memset(&policy, 0, sizeof(policy));
	policy.cbSize = sizeof(policy);
	policy.dwFlags = CERT_CHAIN_POLICY_IGNORE_ALL_REV_UNKNOWN_FLAGS;
	policy.pvExtraPolicyPara = &ssl;
	CERT_CHAIN_POLICY_STATUS status;
	memset(&status, 0, sizeof(status));
	status.cbSize = sizeof(status);
	const DWORD verdict = api->CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status) ? status.dwError : GetLastError();
	api->CertFreeCertificateChain(chain);
	return verdict;
}

// The verdict with only the extra roots trusted.
static DWORD VerdictWithExtraRoots(const TlsApi* api, PCCERT_CONTEXT certificate, const TlsServerCheck* check)
{
	CERT_CHAIN_ENGINE_CONFIG config;
	memset(&config, 0, sizeof(config));
	config.cbSize = sizeof(config);
	config.hExclusiveRoot = check->extraRoots;
	config.dwExclusiveFlags = CERT_CHAIN_EXCLUSIVE_ENABLE_CA_FLAG;
	HCERTCHAINENGINE engine = NULL;
	if (!api->CertCreateCertificateChainEngine(&config, &engine)) return GetLastError();
	const DWORD verdict = Verdict(api, engine, certificate, check);
	api->CertFreeCertificateChainEngine(engine);
	return verdict;
}

static const char* VerdictText(DWORD verdict)
{
	switch (verdict)
	{
	case CERT_E_UNTRUSTEDROOT: return "it is not issued by an authority this machine trusts (an unknown or self-signed root)";
	case CERT_E_CHAINING: return "its chain does not reach a trusted root (an intermediate certificate is missing or unknown)";
	case CERT_E_EXPIRED: return "it has expired, or is not valid yet";
	case CERT_E_CN_NO_MATCH: return "it was issued for other names";
	case CRYPT_E_REVOKED: return "it has been revoked";
	case CERT_E_WRONG_USAGE: return "it is not for server authentication";
	case CERT_E_REVOCATION_FAILURE:
	case CRYPT_E_REVOCATION_OFFLINE:
	case CRYPT_E_NO_REVOCATION_CHECK: return "its revocation status could not be checked";
	case TRUST_E_CERT_SIGNATURE: return "its signature is not valid";
	default: return NULL;
	}
}

int64_t c4_tls_win32_check_server(const TlsApi* api, CtxtHandle* context, const TlsServerCheck* check, char* error, int64_t errorCapacity)
{
	char what[kTlsReasonBytes];
	snprintf(what, sizeof(what), "the certificate of %s is refused", check->hostText);
	PCCERT_CONTEXT certificate = NULL;
	const SECURITY_STATUS status = api->sspi->QueryContextAttributesW(context, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &certificate);
	if (status != SEC_E_OK || !certificate) return c4_tls_win32_explained(what, "the server sent none", error, errorCapacity);
	DWORD verdict = Verdict(api, NULL, certificate, check);
	const bool untrusted = verdict == (DWORD)CERT_E_UNTRUSTEDROOT || verdict == (DWORD)CERT_E_CHAINING;
	if (untrusted && check->extraRoots) verdict = VerdictWithExtraRoots(api, certificate, check);
	api->CertFreeCertificateContext(certificate);
	if (verdict == 0) return c4_tls_win32_done(0, error, errorCapacity);
	const char* reason = VerdictText(verdict);
	return reason ? c4_tls_win32_explained(what, reason, error, errorCapacity) : c4_tls_win32_failed(what, (SECURITY_STATUS)verdict, error, errorCapacity);
}
