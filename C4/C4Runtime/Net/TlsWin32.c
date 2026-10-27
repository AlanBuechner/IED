// TLS sessions for Bedrock's Net namespace (TlsRuntimeC.h) over SChannel: the InitializeSecurityContextW /
// AcceptSecurityContextW handshake loop over the socket, then EncryptMessage and DecryptMessage for each record. A
// client checks the server's certificate itself (TlsTrustWin32.c). A handshake message after the handshake (TLS 1.3's
// tickets and key updates, a TLS 1.2 renegotiation) is answered on the receiving thread, with sends held back until
// its answer is out.

#include "TlsWin32.h"
#include "PathRuntimeC.h"
#include <limits.h>
#include <stdlib.h>

enum
{
	kFirstInputBytes = 16 * 1024,
	kMostInputBytes = 1024 * 1024,    // the largest handshake message (a certificate chain) a session takes
	kHostBytes = 256,
	kRenegotiationMs = 10000,         // the least a handshake after the handshake may take
};

static const ULONGLONG kForever = ~(ULONGLONG)0;

static const DWORD kInitiateFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
	ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_EXTENDED_ERROR | ISC_REQ_USE_SUPPLIED_CREDS;
static const DWORD kAcceptFlags = ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY |
	ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM | ASC_REQ_EXTENDED_ERROR;

typedef struct TlsSession
{
	const TlsApi* api;
	int64_t socket;
	bool server;
	CredHandle credentials;
	bool ownsCredentials;          // a client's own; a server's are its identity's
	CtxtHandle context;
	bool hasContext;
	SecPkgContext_StreamSizes sizes;
	int32_t version;
	wchar_t* host;                 // the client's: SNI and the name the certificate must carry
	char hostText[kHostBytes];     // the same, for messages
	HCERTSTORE extraRoots;         // the client's extra trust anchors, or NULL
	SRWLOCK sendLock;              // one send at a time; a renegotiation holds it too
	SRWLOCK receiveLock;           // one receive at a time
	SRWLOCK contextLock;           // SChannel's calls on the context
	uint8_t* input;                // received, not yet decrypted
	size_t inputSize, inputCapacity;
	bool inputIncomplete;          // the input is part of a record: receive more before decrypting again
	uint8_t* plain;                // decrypted, not yet handed out
	size_t plainStart, plainSize, plainCapacity;
	uint8_t* record;               // the record being sent: header, data, trailer
	bool receiveEnded;
	char receiveFailure[kTlsReasonBytes];
	char sendFailure[kTlsReasonBytes];
	bool closeSent;
} TlsSession;

// --- time ------------------------------------------------------------------------------------------------------------

static ULONGLONG DeadlineAfter(int32_t timeoutMs)
{
	return timeoutMs < 0 ? kForever : GetTickCount64() + (ULONGLONG)timeoutMs;
}

// What is left of a deadline as a timeout for NetRuntimeC.h: kNetWaitForever, or milliseconds (0 once it passed).
static int32_t Remaining(ULONGLONG deadline)
{
	if (deadline == kForever) return kNetWaitForever;
	const ULONGLONG now = GetTickCount64();
	if (now >= deadline) return 0;
	return deadline - now > INT_MAX ? INT_MAX : (int32_t)(deadline - now);
}

// --- the socket ------------------------------------------------------------------------------------------------------

// Every byte, each piece within `pieceMs`: the count, or what the failing send returned.
static int64_t SendAllOn(int64_t socket, const uint8_t* bytes, int64_t count, int32_t pieceMs, char* error, int64_t capacity)
{
	int64_t sent = 0;
	while (sent < count)
	{
		const int64_t now = c4_net_send(socket, bytes + sent, count - sent, NULL, pieceMs, error, capacity);
		if (now <= 0) return now == 0 ? c4_tls_win32_refused("the connection took no bytes", error, capacity) : now;
		sent += now;
	}
	return c4_tls_win32_done(sent, error, capacity);
}

// 1 when the socket can take bytes within `timeoutMs`, 0 when it cannot yet, -1 when it failed.
static int WaitWritable(int64_t socket, int32_t timeoutMs)
{
	if (timeoutMs == kNetWaitForever) return 1;
	c4_net_poll_entry entry = { socket, kNetWritable, 0 };
	const int32_t ready = c4_net_poll(&entry, 1, timeoutMs);
	if (ready < 0 || (entry.ready & kNetFailed)) return -1;
	return ready > 0 && (entry.ready & kNetWritable) ? 1 : 0;
}

static bool GrowInput(TlsSession* s)
{
	if (s->inputCapacity >= kMostInputBytes) return false;
	const size_t wanted = s->inputCapacity ? s->inputCapacity * 2 : kFirstInputBytes;
	uint8_t* grown = (uint8_t*)realloc(s->input, wanted);
	if (!grown) return false;
	s->input = grown;
	s->inputCapacity = wanted;
	return true;
}

// More bytes onto the input: how many, 0 when the peer closed, kNetTimedOut, or -1.
static int64_t ReceiveMore(TlsSession* s, int32_t waitMs, char* error, int64_t capacity)
{
	if (s->inputSize == s->inputCapacity && !GrowInput(s))
		return c4_tls_win32_refused("a TLS message larger than 1 MiB arrived", error, capacity);
	const int64_t got = c4_net_receive(s->socket, s->input + s->inputSize, (int64_t)(s->inputCapacity - s->inputSize), NULL, waitMs, error, capacity);
	if (got > 0)
	{
		s->inputSize += (size_t)got;
		s->inputIncomplete = false;
	}
	return got;
}

// What an SSPI call left unread at the input's end (a SECBUFFER_EXTRA buffer) moves to its front; else none is left.
static void KeepExtra(TlsSession* s, const SecBuffer* extra)
{
	if (extra && extra->BufferType == SECBUFFER_EXTRA && extra->cbBuffer > 0 && extra->cbBuffer <= s->inputSize)
	{
		memmove(s->input, s->input + s->inputSize - extra->cbBuffer, extra->cbBuffer);
		s->inputSize = extra->cbBuffer;
	}
	else s->inputSize = 0;
}

// --- sessions --------------------------------------------------------------------------------------------------------

static TlsSession* NewSession(const TlsApi* api, int64_t socket, bool server)
{
	TlsSession* s = (TlsSession*)calloc(1, sizeof(TlsSession));
	if (!s) return NULL;
	s->api = api;
	s->socket = socket;
	s->server = server;
	InitializeSRWLock(&s->sendLock);
	InitializeSRWLock(&s->receiveLock);
	InitializeSRWLock(&s->contextLock);
	SecInvalidateHandle(&s->credentials);
	SecInvalidateHandle(&s->context);
	return s;
}

static void FreeSession(TlsSession* s)
{
	if (s->hasContext) s->api->sspi->DeleteSecurityContext(&s->context);
	if (s->ownsCredentials) s->api->sspi->FreeCredentialsHandle(&s->credentials);
	if (s->extraRoots) s->api->CertCloseStore(s->extraRoots, 0);
	free(s->host);
	free(s->input);
	free(s->plain);
	free(s->record);
	free(s);
}

// Ends a session that could not start; `result` passed on.
static int64_t Abandon(TlsSession* s, int64_t result)
{
	FreeSession(s);
	return result;
}

static TlsSession* SessionOf(int64_t handle) { return handle == -1 ? NULL : (TlsSession*)(intptr_t)handle; }

// The record sizes and the protocol, once the handshake is done.
static int64_t LearnConnection(TlsSession* s, char* error, int64_t capacity)
{
	SECURITY_STATUS status = s->api->sspi->QueryContextAttributesW(&s->context, SECPKG_ATTR_STREAM_SIZES, &s->sizes);
	if (status != SEC_E_OK) return c4_tls_win32_failed("the TLS session has no record sizes", status, error, capacity);
	SecPkgContext_ConnectionInfo info;
	status = s->api->sspi->QueryContextAttributesW(&s->context, SECPKG_ATTR_CONNECTION_INFO, &info);
	s->version = status == SEC_E_OK && (info.dwProtocol & SP_PROT_TLS1_3) ? kTlsVersion13 : kTlsVersion12;
	s->record = (uint8_t*)malloc((size_t)s->sizes.cbHeader + s->sizes.cbMaximumMessage + s->sizes.cbTrailer);
	if (!s->record) return c4_tls_win32_refused("out of memory for TLS records", error, capacity);
	return c4_tls_win32_done(0, error, capacity);
}

// How long revocation downloads may take: what is left of the handshake (0: the system's own waits).
static DWORD UrlWaitMs(ULONGLONG deadline)
{
	if (deadline == kForever) return 0;
	const int32_t left = Remaining(deadline);
	return left > 0 ? (DWORD)left : 1;
}

static int64_t CheckServer(TlsSession* s, ULONGLONG deadline, char* error, int64_t capacity)
{
	const TlsServerCheck check = { s->host, s->hostText, s->extraRoots, UrlWaitMs(deadline) };
	return c4_tls_win32_check_server(s->api, &s->context, &check, error, capacity);
}

// --- the handshake ---------------------------------------------------------------------------------------------------

// One step of the handshake; `first` is the call that makes the context.
static SECURITY_STATUS HandshakeStep(TlsSession* s, bool first, SecBufferDesc* in, SecBufferDesc* out)
{
	DWORD attributes = 0;
	AcquireSRWLockExclusive(&s->contextLock);
	const SECURITY_STATUS status = s->server
		? s->api->sspi->AcceptSecurityContext(&s->credentials, first ? NULL : &s->context, in, kAcceptFlags, 0, &s->context, out, &attributes, NULL)
		: s->api->sspi->InitializeSecurityContextW(&s->credentials, first ? NULL : &s->context, s->host, kInitiateFlags, 0, 0, in, 0, &s->context, out, &attributes, NULL);
	ReleaseSRWLockExclusive(&s->contextLock);
	return status;
}

static const char* HandshakeProblem(SECURITY_STATUS status)
{
	switch (status)
	{
	case SEC_E_ALGORITHM_MISMATCH:
	case SEC_E_UNSUPPORTED_FUNCTION: return "the two sides share no TLS version or cipher (this side offers TLS 1.2, and 1.3 where Windows has it: Windows 11 and later)";
	case SEC_E_ILLEGAL_MESSAGE:
	case SEC_E_INVALID_TOKEN: return "the peer sent something that is not TLS, or refused the handshake with an alert";
	case SEC_E_MESSAGE_ALTERED:
	case SEC_E_DECRYPT_FAILURE: return "a handshake message was altered or could not be decrypted";
	case SEC_I_INCOMPLETE_CREDENTIALS: return "the server asks for a client certificate, which this client does not have";
	case SEC_E_CERT_UNKNOWN:
	case SEC_E_CERT_EXPIRED:
	case SEC_E_UNTRUSTED_ROOT:
	case SEC_E_WRONG_PRINCIPAL: return "the peer refused this side's certificate";
	case SEC_E_NO_CREDENTIALS:
	case SEC_E_UNKNOWN_CREDENTIALS: return "the certificate's private key cannot be used for TLS";
	default: return NULL;
	}
}

static int64_t HandshakeFailed(const TlsSession* s, SECURITY_STATUS status, char* error, int64_t capacity)
{
	char what[kTlsReasonBytes];
	if (s->server) snprintf(what, sizeof(what), "the TLS handshake failed");
	else snprintf(what, sizeof(what), "the TLS handshake with %s failed", s->hostText);
	const char* problem = HandshakeProblem(status);
	return problem ? c4_tls_win32_explained(what, problem, error, capacity) : c4_tls_win32_failed(what, status, error, capacity);
}

// Sends a step's output token, and frees it: 0, or what the send came to.
static int64_t SendToken(TlsSession* s, SecBuffer* token, int32_t waitMs, char* error, int64_t capacity)
{
	if (!token->pvBuffer) return 0;
	const int64_t sent = token->cbBuffer > 0 ? SendAllOn(s->socket, (const uint8_t*)token->pvBuffer, token->cbBuffer, waitMs, error, capacity) : 0;
	s->api->sspi->FreeContextBuffer(token->pvBuffer);
	token->pvBuffer = NULL;
	return sent < 0 ? sent : 0;
}

// More handshake bytes: 0 when they came, else what to return (the peer closing is an error here).
static int64_t ReceiveHandshake(TlsSession* s, ULONGLONG deadline, char* error, int64_t capacity)
{
	const int64_t got = ReceiveMore(s, Remaining(deadline), error, capacity);
	if (got > 0) return 0;
	if (got == kNetTimedOut) c4_copy_text("the TLS handshake timed out", error, capacity);
	if (got != 0) return got;
	char text[kTlsReasonBytes];
	if (s->server) snprintf(text, sizeof(text), "the client closed the connection during the TLS handshake");
	else snprintf(text, sizeof(text), "%s closed the connection during the TLS handshake (is it serving TLS on this port?)", s->hostText);
	return c4_tls_win32_refused(text, error, capacity);
}

// The handshake from where the session is: a new one (`first`; a server's starts with the client's hello), or one
// after the handshake, whose first step takes the input that is there - a TLS 1.3 message, or nothing, from which a
// TLS 1.2 client makes the hello of its renegotiation.
static int64_t Handshake(TlsSession* s, bool first, ULONGLONG deadline, char* error, int64_t capacity)
{
	bool needInput = first && s->server && s->inputSize == 0;
	for (;;)
	{
		if (needInput)
		{
			const int64_t got = ReceiveHandshake(s, deadline, error, capacity);
			if (got != 0) return got;
		}
		SecBuffer in[2] = { { (unsigned long)s->inputSize, SECBUFFER_TOKEN, s->input }, { 0, SECBUFFER_EMPTY, NULL } };
		SecBufferDesc inDesc = { SECBUFFER_VERSION, 2, in };
		SecBuffer out[1] = { { 0, SECBUFFER_TOKEN, NULL } };
		SecBufferDesc outDesc = { SECBUFFER_VERSION, 1, out };
		const bool hello = first && !s->server;   // the client's first step reads nothing
		const SECURITY_STATUS status = HandshakeStep(s, first, hello ? NULL : &inDesc, &outDesc);
		const bool progressed = status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED;
		if (first && progressed) s->hasContext = true;
		const int64_t sent = SendToken(s, &out[0], Remaining(deadline), error, capacity);   // an alert, too, on a failure
		if (status == SEC_E_INCOMPLETE_MESSAGE)
		{
			needInput = true;
			continue;
		}
		if (!progressed) return HandshakeFailed(s, status, error, capacity);
		if (sent < 0) return sent;
		if (!hello) KeepExtra(s, &in[1]);
		first = false;
		if (status == SEC_E_OK) return c4_tls_win32_done(0, error, capacity);
		needInput = s->inputSize == 0;
	}
}

// --- client and server -----------------------------------------------------------------------------------------------

// A client session's host name, extra roots and credentials; 0, or -1 with why.
static int64_t PrepareClient(TlsSession* s, const c4_tls_client_options* options, char* error, int64_t capacity)
{
	c4_copy_text(options->hostName, s->hostText, sizeof(s->hostText));
	s->host = c4_utf8_to_wide(options->hostName);
	if (!s->host) return c4_tls_win32_refused("the host name is not UTF-8", error, capacity);
	if (options->extraRootsSize > 0 && !c4_tls_win32_load_roots(s->api, options->extraRoots, options->extraRootsSize, &s->extraRoots, error, capacity))
		return kNetError;
	const SECURITY_STATUS acquired = c4_tls_win32_credentials(s->api, false, NULL, &s->credentials);
	if (acquired != SEC_E_OK) return c4_tls_win32_failed("SChannel refused TLS client credentials", acquired, error, capacity);
	s->ownsCredentials = true;
	return 0;
}

int64_t c4_tls_connect(int64_t socket, const c4_tls_client_options* options, int32_t timeoutMs, char* error, int64_t errorCapacity)
{
	if (socket < 0 || !options || !options->hostName || !*options->hostName)
		return c4_tls_win32_refused("a connected socket and a host name are needed", error, errorCapacity);
	if (options->extraRootsSize < 0 || (options->extraRootsSize > 0 && !options->extraRoots))
		return c4_tls_win32_refused("the extra roots are malformed", error, errorCapacity);
	const TlsApi* api = c4_tls_win32_api(error, errorCapacity);
	if (!api) return kNetError;
	TlsSession* s = NewSession(api, socket, false);
	if (!s) return c4_tls_win32_refused("out of memory for a TLS session", error, errorCapacity);
	const ULONGLONG deadline = DeadlineAfter(timeoutMs);
	int64_t result = PrepareClient(s, options, error, errorCapacity);
	if (result == 0) result = Handshake(s, true, deadline, error, errorCapacity);
	if (result == 0) result = CheckServer(s, deadline, error, errorCapacity);
	if (result == 0) result = LearnConnection(s, error, errorCapacity);
	if (result != 0) return Abandon(s, result);
	return c4_tls_win32_done((int64_t)(intptr_t)s, error, errorCapacity);
}

int64_t c4_tls_accept(int64_t socket, int64_t identity, int32_t timeoutMs, char* error, int64_t errorCapacity)
{
	const TlsIdentity* id = identity == -1 ? NULL : (const TlsIdentity*)(intptr_t)identity;
	if (socket < 0 || !id) return c4_tls_win32_refused("an accepted socket and an identity are needed", error, errorCapacity);
	TlsSession* s = NewSession(id->api, socket, true);
	if (!s) return c4_tls_win32_refused("out of memory for a TLS session", error, errorCapacity);
	s->credentials = id->credentials;
	int64_t result = Handshake(s, true, DeadlineAfter(timeoutMs), error, errorCapacity);
	if (result == 0) result = LearnConnection(s, error, errorCapacity);
	if (result != 0) return Abandon(s, result);
	return c4_tls_win32_done((int64_t)(intptr_t)s, error, errorCapacity);
}

// --- sending ---------------------------------------------------------------------------------------------------------

// Up to one record of `data` encrypted and sent, with the send lock held: the bytes it took, or the failure (a record
// cut off breaks the session).
static int64_t SendRecord(TlsSession* s, const void* data, int64_t size, int32_t timeoutMs, char* error, int64_t capacity)
{
	const DWORD count = (DWORD)(size < (int64_t)s->sizes.cbMaximumMessage ? size : (int64_t)s->sizes.cbMaximumMessage);
	uint8_t* record = s->record;
	memcpy(record + s->sizes.cbHeader, data, count);
	SecBuffer buffers[4] = {
		{ s->sizes.cbHeader, SECBUFFER_STREAM_HEADER, record },
		{ count, SECBUFFER_DATA, record + s->sizes.cbHeader },
		{ s->sizes.cbTrailer, SECBUFFER_STREAM_TRAILER, record + s->sizes.cbHeader + count },
		{ 0, SECBUFFER_EMPTY, NULL } };
	SecBufferDesc desc = { SECBUFFER_VERSION, 4, buffers };
	AcquireSRWLockExclusive(&s->contextLock);
	const SECURITY_STATUS status = s->api->sspi->EncryptMessage(&s->context, 0, &desc, 0);
	ReleaseSRWLockExclusive(&s->contextLock);
	if (status != SEC_E_OK) return c4_tls_win32_failed("a TLS record could not be encrypted", status, error, capacity);
	const int64_t total = (int64_t)buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
	const int64_t sent = SendAllOn(s->socket, record, total, timeoutMs, error, capacity);
	if (sent == total) return c4_tls_win32_done(count, error, capacity);
	snprintf(s->sendFailure, sizeof(s->sendFailure), "a TLS record was cut off (%s): the session cannot send again", sent == kNetTimedOut ? "timed out" : error);
	return sent;
}

// The send with its lock held: refused once the session cannot send, timed out with nothing encrypted while the
// socket takes nothing.
static int64_t SendHeld(TlsSession* s, const void* data, int64_t size, int32_t timeoutMs, char* error, int64_t capacity)
{
	if (s->sendFailure[0]) return c4_tls_win32_refused(s->sendFailure, error, capacity);
	if (s->closeSent) return c4_tls_win32_refused("this side has closed the TLS session", error, capacity);
	if (size == 0) return c4_tls_win32_done(0, error, capacity);
	const int writable = WaitWritable(s->socket, timeoutMs);
	if (writable < 0) return c4_tls_win32_refused("the connection failed", error, capacity);
	if (writable == 0)
	{
		c4_copy_text("timed out", error, capacity);
		return kNetTimedOut;
	}
	return SendRecord(s, data, size, timeoutMs, error, capacity);
}

int64_t c4_tls_send(int64_t session, const void* data, int64_t size, int32_t timeoutMs, char* error, int64_t errorCapacity)
{
	TlsSession* s = SessionOf(session);
	if (!s || size < 0 || (size > 0 && !data)) return c4_tls_win32_refused("nothing to send", error, errorCapacity);
	AcquireSRWLockExclusive(&s->sendLock);
	const int64_t result = SendHeld(s, data, size, timeoutMs, error, errorCapacity);
	ReleaseSRWLockExclusive(&s->sendLock);
	return result;
}

// --- receiving -------------------------------------------------------------------------------------------------------

// `bytes` as the plaintext to hand out.
static bool KeepPlain(TlsSession* s, const SecBuffer* data)
{
	if (data->cbBuffer > s->plainCapacity)
	{
		uint8_t* grown = (uint8_t*)realloc(s->plain, data->cbBuffer);
		if (!grown) return false;
		s->plain = grown;
		s->plainCapacity = data->cbBuffer;
	}
	memcpy(s->plain, data->pvBuffer, data->cbBuffer);
	s->plainStart = 0;
	s->plainSize = data->cbBuffer;
	return true;
}

// The input's first record decrypted, if it is whole: its plaintext to hand out, the rest kept. SEC_E_OK,
// SEC_E_INCOMPLETE_MESSAGE, SEC_I_CONTEXT_EXPIRED (close_notify), SEC_I_RENEGOTIATE (a handshake message, in the input
// now), or a failure.
static SECURITY_STATUS DecryptRecord(TlsSession* s)
{
	SecBuffer buffers[4] = { { (unsigned long)s->inputSize, SECBUFFER_DATA, s->input }, { 0, SECBUFFER_EMPTY, NULL },
		{ 0, SECBUFFER_EMPTY, NULL }, { 0, SECBUFFER_EMPTY, NULL } };
	SecBufferDesc desc = { SECBUFFER_VERSION, 4, buffers };
	AcquireSRWLockExclusive(&s->contextLock);
	const SECURITY_STATUS status = s->api->sspi->DecryptMessage(&s->context, &desc, 0, NULL);
	ReleaseSRWLockExclusive(&s->contextLock);
	if (status == SEC_E_INCOMPLETE_MESSAGE) s->inputIncomplete = true;
	if (status != SEC_E_OK && status != SEC_I_RENEGOTIATE && status != SEC_I_CONTEXT_EXPIRED) return status;
	const SecBuffer* data = NULL;
	const SecBuffer* extra = NULL;
	for (int i = 1; i < 4; ++i)
	{
		if (buffers[i].BufferType == SECBUFFER_DATA) data = &buffers[i];
		if (buffers[i].BufferType == SECBUFFER_EXTRA) extra = &buffers[i];
	}
	if (data && data->cbBuffer > 0 && !KeepPlain(s, data)) return SEC_E_INSUFFICIENT_MEMORY;
	KeepExtra(s, extra);
	return status;
}

// A handshake message after the handshake, answered before any other send goes out (a TLS 1.3 key update must
// precede records under the new key). It runs to its end - at least kRenegotiationMs, past a receive's shorter
// wait - since no record can be sent or read while it is under way. A TLS 1.2 renegotiation may bring another
// certificate, which is checked again.
static int64_t Renegotiate(TlsSession* s, ULONGLONG deadline, char* error, int64_t capacity)
{
	const ULONGLONG least = DeadlineAfter(kRenegotiationMs);
	if (deadline < least) deadline = least;
	AcquireSRWLockExclusive(&s->sendLock);
	int64_t result = Handshake(s, false, deadline, error, capacity);
	if (result == 0 && !s->server && s->version == kTlsVersion12) result = CheckServer(s, deadline, error, capacity);
	if (result != 0)
	{
		snprintf(s->receiveFailure, sizeof(s->receiveFailure), "the TLS renegotiation failed (%s): the session cannot go on", error);
		c4_copy_text(s->receiveFailure, s->sendFailure, sizeof(s->sendFailure));
	}
	ReleaseSRWLockExclusive(&s->sendLock);
	return result;
}

// What decrypting the input came to: true to go on (plaintext, the end, a failure kept, more to receive), false with
// `*result` when a renegotiation could not finish.
static bool DecryptStep(TlsSession* s, ULONGLONG deadline, int64_t* result, char* error, int64_t capacity)
{
	const SECURITY_STATUS status = DecryptRecord(s);
	if (status == SEC_I_CONTEXT_EXPIRED) s->receiveEnded = true;
	else if (status == SEC_I_RENEGOTIATE)
	{
		*result = Renegotiate(s, deadline, error, capacity);
		return *result == 0;
	}
	else if (status != SEC_E_OK && status != SEC_E_INCOMPLETE_MESSAGE)
		c4_tls_win32_failed("a TLS record could not be decrypted (altered or corrupt)", status, s->receiveFailure, sizeof(s->receiveFailure));
	return true;
}

// One step towards plaintext, decrypting what is in or receiving more: true to go on, false with `*result`.
static bool ReceiveStep(TlsSession* s, ULONGLONG deadline, int64_t* result, char* error, int64_t capacity)
{
	if (s->inputSize > 0 && !s->inputIncomplete) return DecryptStep(s, deadline, result, error, capacity);
	const int64_t got = ReceiveMore(s, Remaining(deadline), error, capacity);
	if (got > 0) return true;
	if (got != 0)
	{
		*result = got;
		return false;
	}
	if (s->inputSize > 0) c4_copy_text("the connection ended inside a TLS record", s->receiveFailure, sizeof(s->receiveFailure));
	else s->receiveEnded = true;
	return true;
}

static int64_t HandOut(TlsSession* s, void* buffer, int64_t capacity)
{
	const size_t count = (size_t)capacity < s->plainSize ? (size_t)capacity : s->plainSize;
	memcpy(buffer, s->plain + s->plainStart, count);
	s->plainStart += count;
	s->plainSize -= count;
	return (int64_t)count;
}

// The receive with its lock held.
static int64_t ReceiveHeld(TlsSession* s, void* buffer, int64_t capacity, int32_t timeoutMs, char* error, int64_t errorCapacity)
{
	const ULONGLONG deadline = DeadlineAfter(timeoutMs);
	int64_t result = 0;
	for (;;)
	{
		if (s->plainSize > 0) return c4_tls_win32_done(HandOut(s, buffer, capacity), error, errorCapacity);
		if (s->receiveFailure[0]) return c4_tls_win32_refused(s->receiveFailure, error, errorCapacity);
		if (s->receiveEnded) return c4_tls_win32_done(0, error, errorCapacity);
		if (!ReceiveStep(s, deadline, &result, error, errorCapacity)) return result;
	}
}

int64_t c4_tls_receive(int64_t session, void* buffer, int64_t capacity, int32_t timeoutMs, char* error, int64_t errorCapacity)
{
	TlsSession* s = SessionOf(session);
	if (!s || capacity <= 0 || !buffer) return c4_tls_win32_refused("no room to receive into", error, errorCapacity);
	AcquireSRWLockExclusive(&s->receiveLock);
	const int64_t result = ReceiveHeld(s, buffer, capacity, timeoutMs, error, errorCapacity);
	ReleaseSRWLockExclusive(&s->receiveLock);
	return result;
}

// --- ending ----------------------------------------------------------------------------------------------------------

// The close_notify alert, with the send lock held: 0 when it went out.
static int32_t SendCloseNotify(TlsSession* s)
{
	DWORD type = SCHANNEL_SHUTDOWN;
	SecBuffer control = { sizeof(type), SECBUFFER_TOKEN, &type };
	SecBufferDesc controlDesc = { SECBUFFER_VERSION, 1, &control };
	AcquireSRWLockExclusive(&s->contextLock);
	SECURITY_STATUS status = s->api->sspi->ApplyControlToken(&s->context, &controlDesc);
	ReleaseSRWLockExclusive(&s->contextLock);
	s->closeSent = true;
	if (status != SEC_E_OK) return kNetError;
	SecBuffer out[1] = { { 0, SECBUFFER_TOKEN, NULL } };
	SecBufferDesc outDesc = { SECBUFFER_VERSION, 1, out };
	status = HandshakeStep(s, false, NULL, &outDesc);
	char ignored[kTlsReasonBytes];
	const bool made = status == SEC_E_OK || status == SEC_I_CONTEXT_EXPIRED;
	const bool sent = made && out[0].pvBuffer && out[0].cbBuffer > 0 && SendToken(s, &out[0], 0, ignored, sizeof(ignored)) == 0;
	if (out[0].pvBuffer) s->api->sspi->FreeContextBuffer(out[0].pvBuffer);
	return sent ? 0 : kNetError;
}

int32_t c4_tls_close_notify(int64_t session)
{
	TlsSession* s = SessionOf(session);
	if (!s || !TryAcquireSRWLockExclusive(&s->sendLock)) return kNetError;
	const bool possible = !s->closeSent && !s->sendFailure[0] && WaitWritable(s->socket, 0) == 1;
	const int32_t result = possible ? SendCloseNotify(s) : kNetError;
	ReleaseSRWLockExclusive(&s->sendLock);
	return result;
}

int32_t c4_tls_version(int64_t session)
{
	const TlsSession* s = SessionOf(session);
	return s ? s->version : kTlsVersionNone;
}

void c4_tls_free(int64_t session)
{
	TlsSession* s = SessionOf(session);
	if (!s) return;
	// A call still in the session finishes first.
	AcquireSRWLockExclusive(&s->receiveLock);
	AcquireSRWLockExclusive(&s->sendLock);
	ReleaseSRWLockExclusive(&s->sendLock);
	ReleaseSRWLockExclusive(&s->receiveLock);
	FreeSession(s);
}
