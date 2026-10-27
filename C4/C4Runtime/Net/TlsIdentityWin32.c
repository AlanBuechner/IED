// SChannel credentials (TlsWin32.h) and a TLS server's identity (TlsRuntimeC.h): a certificate and its private key
// imported from PKCS #12 bytes. SChannel runs in LSASS, which cannot use a key that lives only in this process
// (PKCS12_NO_PERSIST_KEY), so the import puts the key in the user's key store under a new name, and the identity
// deletes it there when it is released.

#include "TlsWin32.h"
#include "PathRuntimeC.h"
#include <ncrypt.h>
#include <limits.h>
#include <stdlib.h>

#pragma comment(lib, "ncrypt.lib")

SECURITY_STATUS c4_tls_win32_credentials(bool server, PCCERT_CONTEXT* certificate, CredHandle* out)
{
	const DWORD direction = server ? SECPKG_CRED_INBOUND : SECPKG_CRED_OUTBOUND;
	const DWORD flags = SCH_USE_STRONG_CRYPTO | (server ? 0 : SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS);
	TLS_PARAMETERS parameters;
	memset(&parameters, 0, sizeof(parameters));
	parameters.grbitDisabledProtocols = (DWORD)~(server ? SP_PROT_TLS1_2_SERVER | SP_PROT_TLS1_3_SERVER : SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT);
	SCH_CREDENTIALS modern;
	memset(&modern, 0, sizeof(modern));
	modern.dwVersion = SCH_CREDENTIALS_VERSION;
	modern.dwFlags = flags;
	modern.cCreds = certificate ? 1 : 0;
	modern.paCred = certificate;
	modern.cTlsParameters = 1;
	modern.pTlsParameters = &parameters;
	if (AcquireCredentialsHandleW(NULL, (LPWSTR)UNISP_NAME_W, direction, NULL, &modern, NULL, NULL, out, NULL) == SEC_E_OK) return SEC_E_OK;
	SCHANNEL_CRED legacy;
	memset(&legacy, 0, sizeof(legacy));
	legacy.dwVersion = SCHANNEL_CRED_VERSION;
	legacy.dwFlags = flags;
	legacy.cCreds = certificate ? 1 : 0;
	legacy.paCred = certificate;
	legacy.grbitEnabledProtocols = server ? SP_PROT_TLS1_2_SERVER : SP_PROT_TLS1_2_CLIENT;
	return AcquireCredentialsHandleW(NULL, (LPWSTR)UNISP_NAME_W, direction, NULL, &legacy, NULL, NULL, out, NULL);
}

// The key the import stored for `certificate` (always with CNG: PKCS12_ALWAYS_CNG_KSP), deleted from the key store.
static void DeleteStoredKey(PCCERT_CONTEXT certificate)
{
	DWORD size = 0;
	if (!CertGetCertificateContextProperty(certificate, CERT_KEY_PROV_INFO_PROP_ID, NULL, &size)) return;
	CRYPT_KEY_PROV_INFO* info = (CRYPT_KEY_PROV_INFO*)malloc(size);
	NCRYPT_PROV_HANDLE provider = 0;
	NCRYPT_KEY_HANDLE key = 0;
	if (info && CertGetCertificateContextProperty(certificate, CERT_KEY_PROV_INFO_PROP_ID, info, &size) &&
		NCryptOpenStorageProvider(&provider, info->pwszProvName, 0) == ERROR_SUCCESS &&
		NCryptOpenKey(provider, &key, info->pwszContainerName, 0, (info->dwFlags & CRYPT_MACHINE_KEYSET) ? NCRYPT_MACHINE_KEY_FLAG : 0) == ERROR_SUCCESS)
		NCryptDeleteKey(key, 0);
	if (provider) NCryptFreeObject(provider);
	free(info);
}

// Every key the import stored.
static void DeleteStoredKeys(HCERTSTORE store)
{
	for (PCCERT_CONTEXT certificate = CertEnumCertificatesInStore(store, NULL); certificate; certificate = CertEnumCertificatesInStore(store, certificate))
		DeleteStoredKey(certificate);
}

static void FreeIdentity(TlsIdentity* id)
{
	if (SecIsValidHandle(&id->credentials)) FreeCredentialsHandle(&id->credentials);
	if (id->certificate) CertFreeCertificateContext(id->certificate);
	if (id->store)
	{
		DeleteStoredKeys(id->store);
		CertCloseStore(id->store, 0);
	}
	free(id);
}

static int64_t Abandon(TlsIdentity* id, int64_t result)
{
	FreeIdentity(id);
	return result;
}

// The PKCS #12 file's certificates, their keys stored under new names; NULL with the system's error.
static HCERTSTORE Import(const void* pkcs12, int64_t size, const char* password, DWORD* problem)
{
	wchar_t* wide = c4_utf8_to_wide(password ? password : "");
	if (!wide)
	{
		*problem = ERROR_NO_UNICODE_TRANSLATION;
		return NULL;
	}
	CRYPT_DATA_BLOB blob = { (DWORD)size, (BYTE*)pkcs12 };
	HCERTSTORE store = PFXImportCertStore(&blob, wide, PKCS12_ALWAYS_CNG_KSP);
	*problem = store ? 0 : GetLastError();
	SecureZeroMemory(wide, wcslen(wide) * sizeof(wchar_t));
	free(wide);
	return store;
}

int64_t c4_tls_identity_load(const void* pkcs12, int64_t size, const char* password, char* error, int64_t errorCapacity)
{
	if (!pkcs12 || size <= 0 || size > INT_MAX) return c4_tls_win32_refused("no PKCS #12 bytes", error, errorCapacity);
	CRYPT_DATA_BLOB blob = { (DWORD)size, (BYTE*)pkcs12 };
	if (!PFXIsPFXBlob(&blob)) return c4_tls_win32_refused("not a PKCS #12 (.pfx) file", error, errorCapacity);
	TlsIdentity* id = (TlsIdentity*)calloc(1, sizeof(TlsIdentity));
	if (!id) return c4_tls_win32_refused("out of memory for a TLS identity", error, errorCapacity);
	SecInvalidateHandle(&id->credentials);
	DWORD problem = 0;
	id->store = Import(pkcs12, size, password, &problem);
	if (problem == ERROR_INVALID_PASSWORD) return Abandon(id, c4_tls_win32_refused("the password does not open the PKCS #12 file", error, errorCapacity));
	if (problem == ERROR_NO_UNICODE_TRANSLATION) return Abandon(id, c4_tls_win32_refused("the password is not UTF-8", error, errorCapacity));
	if (!id->store) return Abandon(id, c4_tls_win32_failed("the PKCS #12 file cannot be read", (SECURITY_STATUS)problem, error, errorCapacity));
	id->certificate = CertFindCertificateInStore(id->store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_HAS_PRIVATE_KEY, NULL, NULL);
	if (!id->certificate) return Abandon(id, c4_tls_win32_refused("the PKCS #12 file holds no certificate with its private key", error, errorCapacity));
	const SECURITY_STATUS acquired = c4_tls_win32_credentials(true, &id->certificate, &id->credentials);
	if (acquired != SEC_E_OK)
	{
		SecInvalidateHandle(&id->credentials);
		return Abandon(id, c4_tls_win32_failed("SChannel refused the TLS identity", acquired, error, errorCapacity));
	}
	return c4_tls_win32_done((int64_t)(intptr_t)id, error, errorCapacity);
}

void c4_tls_identity_free(int64_t identity)
{
	if (identity != -1 && identity != 0) FreeIdentity((TlsIdentity*)(intptr_t)identity);
}
