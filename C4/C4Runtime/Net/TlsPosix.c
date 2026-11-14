// TLS sessions for Bedrock's Net namespace (TlsRuntimeC.h) over OpenSSL, with the socket kept out of its reach: OpenSSL
// reads the bytes received from one memory BIO and writes what to send into another, and this file moves them over
// the socket with NetRuntimeC.h's calls, so every wait keeps its timeout and the peer's end is told apart from an end
// inside a record, as on Windows. OpenSSL's calls on a session are serialized (contextLock), never the socket's: a
// send and a receive run at once. Whoever holds the send lock sends what OpenSSL wrote, in the order it wrote it - a
// record, or the answer a receive made to a message after the handshake (a TLS 1.3 key update, an alert), which the
// receiving thread sends at once, holding back other sends until it is out. A client checks the server's certificate
// once the handshake is done (TlsTrustPosix.c).

#include "TlsPosix.h"
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

enum
{
	kChunkBytes = 16 * 1024 + 512,    // a record of the most plaintext (16 KiB) with its header and protection
	kMostRecordBytes = 16 * 1024,     // the plaintext of one record
	kHostBytes = 256,
	kAnswerMs = 10000,                // the least an answer to the peer's message after the handshake may take
};

typedef struct TlsSession
{
	const TlsApi* api;
	int64_t socket;
	bool server;
	SSL* ssl;
	BIO* incoming;                    // received, for OpenSSL to read (owned by ssl)
	BIO* outgoing;                    // what OpenSSL wrote, to send (owned by ssl)
	bool broken;                      // OpenSSL failed on the session: no close_notify after that
	int32_t version;
	char host[kHostBytes];            // the client's: SNI and the name the certificate must carry
	X509_STORE* extraRoots;           // the client's extra trust anchors, or NULL
	pthread_mutex_t sendLock;         // one send at a time, and with it the sending of what OpenSSL wrote
	pthread_mutex_t receiveLock;      // one receive at a time
	pthread_mutex_t contextLock;      // OpenSSL's calls on the session and its BIOs
	uint8_t received[kChunkBytes];    // the receiving thread's: from the socket into `incoming`
	uint8_t sending[kChunkBytes];     // the send lock holder's: from `outgoing` onto the socket
	bool receiveEnded;
	char receiveFailure[kTlsReasonBytes];
	char sendFailure[kTlsReasonBytes];
	bool closeSent;
} TlsSession;

// What one OpenSSL call on the session came to.
typedef struct TlsStep
{
	int result;              // what the call returned
	int status;              // SSL_get_error's verdict on it when it is not a success
	unsigned long failure;   // the first error OpenSSL queued, 0 when none
	bool answered;           // OpenSSL wrote something to send
} TlsStep;

// --- time ------------------------------------------------------------------------------------------------------------

static int64_t NowMs(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static const int64_t kForever = -1;

static int64_t DeadlineAfter(int32_t timeoutMs) { return timeoutMs < 0 ? kForever : NowMs() + timeoutMs; }

// What is left of a deadline as a timeout for NetRuntimeC.h: kNetWaitForever, or milliseconds (0 once it passed).
static int32_t Remaining(int64_t deadline)
{
	if (deadline == kForever) return kNetWaitForever;
	const int64_t left = deadline - NowMs();
	return left <= 0 ? 0 : left > INT_MAX ? INT_MAX : (int32_t)left;
}

// --- the socket ------------------------------------------------------------------------------------------------------

// Every byte, each piece within `pieceMs`: the count, or what the failing send returned.
static int64_t SendAllOn(int64_t socket, const uint8_t* bytes, int64_t count, int32_t pieceMs, char* error, int64_t capacity)
{
	int64_t sent = 0;
	while (sent < count)
	{
		const int64_t now = c4_net_send(socket, bytes + sent, count - sent, NULL, pieceMs, error, capacity);
		if (now <= 0) return now == 0 ? c4_tls_posix_refused("the connection took no bytes", error, capacity) : now;
		sent += now;
	}
	return c4_tls_posix_done(sent, error, capacity);
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

// What OpenSSL wrote, sent in order, each piece within `pieceMs`: 0, or what the failing send returned. The caller
// holds the send lock (or runs the handshake, when no other call can be in the session).
static int64_t SendWritten(TlsSession* s, int32_t pieceMs, char* error, int64_t capacity)
{
	for (;;)
	{
		pthread_mutex_lock(&s->contextLock);
		const int count = s->api->BIO_read(s->outgoing, s->sending, sizeof(s->sending));
		pthread_mutex_unlock(&s->contextLock);
		if (count <= 0) return c4_tls_posix_done(0, error, capacity);
		const int64_t sent = SendAllOn(s->socket, s->sending, count, pieceMs, error, capacity);
		if (sent < 0) return sent;
	}
}

// More bytes from the socket for OpenSSL to read: how many, 0 when the peer closed, kNetTimedOut, or -1.
static int64_t ReceiveMore(TlsSession* s, int32_t waitMs, char* error, int64_t capacity)
{
	const int64_t got = c4_net_receive(s->socket, s->received, sizeof(s->received), NULL, waitMs, error, capacity);
	if (got <= 0) return got;
	pthread_mutex_lock(&s->contextLock);
	const int kept = s->api->BIO_write(s->incoming, s->received, (int)got);
	pthread_mutex_unlock(&s->contextLock);
	return kept == got ? got : c4_tls_posix_refused("out of memory for received TLS bytes", error, capacity);
}

// Whether part of a record has arrived and waits for the rest.
static bool RecordUnfinished(TlsSession* s)
{
	pthread_mutex_lock(&s->contextLock);
	const bool unfinished = s->api->BIO_ctrl_pending(s->incoming) > 0 || s->api->SSL_has_pending(s->ssl);
	pthread_mutex_unlock(&s->contextLock);
	return unfinished;
}

// --- OpenSSL's calls -------------------------------------------------------------------------------------------------

typedef enum TlsCall { kCallHandshake, kCallRead, kCallWrite, kCallShutdown } TlsCall;

// One call on the session, under the context lock: what it came to, OpenSSL's errors taken off the queue.
static TlsStep Call(TlsSession* s, TlsCall call, void* data, int size)
{
	const TlsApi* api = s->api;
	TlsStep step = { 0, 0, 0, false };
	pthread_mutex_lock(&s->contextLock);
	api->ERR_clear_error();
	switch (call)
	{
	case kCallHandshake: step.result = api->SSL_do_handshake(s->ssl); break;
	case kCallRead: step.result = api->SSL_read(s->ssl, data, size); break;
	case kCallWrite: step.result = api->SSL_write(s->ssl, data, size); break;
	case kCallShutdown: step.result = s->broken ? -1 : api->SSL_shutdown(s->ssl); break;
	}
	const bool succeeded = call == kCallHandshake ? step.result == 1 : call == kCallShutdown ? step.result >= 0 : step.result > 0;
	step.status = succeeded || (call == kCallShutdown && s->broken) ? 0 : api->SSL_get_error(s->ssl, step.result);
	step.failure = c4_tls_posix_take_error(api);
	if (step.status == kSslErrorSsl || step.status == kSslErrorSyscall) s->broken = true;
	step.answered = api->BIO_ctrl_pending(s->outgoing) > 0;
	pthread_mutex_unlock(&s->contextLock);
	return step;
}

// Whether the failure is an alert the peer sent.
static bool IsAlert(const TlsApi* api, unsigned long failure)
{
	if (c4_tls_posix_error_library(api, failure) != kTlsLibrarySsl) return false;
	const int reason = c4_tls_posix_error_reason(api, failure);
	return reason >= kSslAlertReasonOffset && reason < kSslAlertReasonOffset + 256;
}

// --- sessions --------------------------------------------------------------------------------------------------------

static TlsSession* NewSession(const TlsApi* api, SSL_CTX* context, int64_t socket, bool server)
{
	TlsSession* s = (TlsSession*)calloc(1, sizeof(TlsSession));
	if (!s) return NULL;
	s->api = api;
	s->socket = socket;
	s->server = server;
	pthread_mutex_init(&s->sendLock, NULL);
	pthread_mutex_init(&s->receiveLock, NULL);
	pthread_mutex_init(&s->contextLock, NULL);
	s->ssl = api->SSL_new(context);
	s->incoming = api->BIO_new(api->BIO_s_mem());
	s->outgoing = api->BIO_new(api->BIO_s_mem());
	if (s->ssl && s->incoming && s->outgoing)
	{
		api->SSL_set_bio(s->ssl, s->incoming, s->outgoing);
		if (server) api->SSL_set_accept_state(s->ssl);
		else api->SSL_set_connect_state(s->ssl);
	}
	return s;
}

static void FreeSession(TlsSession* s)
{
	const TlsApi* api = s->api;
	if (s->ssl && s->incoming && s->outgoing) api->SSL_free(s->ssl);   // and its BIOs
	else
	{
		if (s->ssl) api->SSL_free(s->ssl);
		if (s->incoming) api->BIO_free(s->incoming);
		if (s->outgoing) api->BIO_free(s->outgoing);
	}
	if (s->extraRoots) api->X509_STORE_free(s->extraRoots);
	pthread_mutex_destroy(&s->sendLock);
	pthread_mutex_destroy(&s->receiveLock);
	pthread_mutex_destroy(&s->contextLock);
	free(s);
}

// Ends a session that could not start; `result` passed on.
static int64_t Abandon(TlsSession* s, int64_t result)
{
	FreeSession(s);
	return result;
}

// A new session on `socket`, ready for its handshake; 0, or -1 with why.
static int64_t Opened(TlsSession* s, char* error, int64_t capacity)
{
	if (!s) return c4_tls_posix_refused("out of memory for a TLS session", error, capacity);
	if (s->ssl && s->incoming && s->outgoing) return 0;
	return c4_tls_posix_failed(s->api, "OpenSSL could not make a TLS session", c4_tls_posix_take_error(s->api), error, capacity);
}

static TlsSession* SessionOf(int64_t handle) { return handle == -1 ? NULL : (TlsSession*)(intptr_t)handle; }

// The protocol, once the handshake is done.
static void LearnConnection(TlsSession* s)
{
	s->version = s->api->SSL_version(s->ssl) == kTls13Version ? kTlsVersion13 : kTlsVersion12;
}

// --- the handshake ---------------------------------------------------------------------------------------------------

static const char* HandshakeProblem(const TlsApi* api, unsigned long failure)
{
	enum
	{
		kNoSuitableSignatureAlgorithm = 118, kHttpsProxyRequest = 155, kHttpRequest = 156, kDecryptionFailed = 147,
		kNoProtocolsAvailable = 191, kNoSharedCipher = 193, kPacketLengthTooLong = 198, kUnexpectedMessage = 244,
		kUnexpectedRecord = 245, kUnknownProtocol = 252, kUnsupportedProtocol = 258, kWrongSslVersion = 266,
		kWrongVersionNumber = 267, kDecryptionFailedOrBadRecordMac = 281, kNoSharedGroups = 410, kBadRecordType = 443,
		kAlertBadCertificate = 42, kAlertUnsupportedCertificate = 43, kAlertCertificateRevoked = 44,
		kAlertCertificateExpired = 45, kAlertCertificateUnknown = 46, kAlertUnknownCa = 48, kAlertProtocolVersion = 70,
		kAlertInsufficientSecurity = 71, kAlertCertificateRequired = 116,
	};
	if (c4_tls_posix_error_library(api, failure) != kTlsLibrarySsl) return NULL;
	const int reason = c4_tls_posix_error_reason(api, failure);
	switch (reason)
	{
	case kNoSharedCipher:
	case kUnsupportedProtocol:
	case kNoProtocolsAvailable:
	case kNoSuitableSignatureAlgorithm:
	case kNoSharedGroups:
	case kSslAlertReasonOffset + kAlertProtocolVersion:
	case kSslAlertReasonOffset + kAlertInsufficientSecurity: return "the two sides share no TLS version or cipher (this side offers TLS 1.2 and 1.3)";
	case kWrongVersionNumber:
	case kWrongSslVersion:
	case kUnknownProtocol:
	case kHttpRequest:
	case kHttpsProxyRequest:
	case kPacketLengthTooLong:
	case kBadRecordType:
	case kUnexpectedMessage:
	case kUnexpectedRecord: return "the peer sent something that is not TLS, or refused the handshake with an alert";
	case kDecryptionFailed:
	case kDecryptionFailedOrBadRecordMac: return "a handshake message was altered or could not be decrypted";
	case kSslAlertReasonOffset + kAlertCertificateRequired: return "the server asks for a client certificate, which this client does not have";
	case kSslAlertReasonOffset + kAlertBadCertificate:
	case kSslAlertReasonOffset + kAlertUnsupportedCertificate:
	case kSslAlertReasonOffset + kAlertCertificateRevoked:
	case kSslAlertReasonOffset + kAlertCertificateExpired:
	case kSslAlertReasonOffset + kAlertCertificateUnknown:
	case kSslAlertReasonOffset + kAlertUnknownCa: return "the peer refused this side's certificate";
	default: return IsAlert(api, failure) ? "the peer sent something that is not TLS, or refused the handshake with an alert" : NULL;
	}
}

static int64_t HandshakeFailed(const TlsSession* s, unsigned long failure, char* error, int64_t capacity)
{
	char what[kTlsReasonBytes];
	if (s->server) snprintf(what, sizeof(what), "the TLS handshake failed");
	else snprintf(what, sizeof(what), "the TLS handshake with %s failed", s->host);
	char reason[kTlsReasonBytes];
	c4_tls_posix_error_text(s->api, failure, reason, sizeof(reason));
	const char* problem = HandshakeProblem(s->api, failure);
	if (!problem) return c4_tls_posix_explained(what, reason, error, capacity);
	char text[kTlsReasonBytes * 2];
	snprintf(text, sizeof(text), "%s (%s)", problem, reason);
	return c4_tls_posix_explained(what, text, error, capacity);
}

// More handshake bytes: 0 when they came, else what to return (the peer closing is an error here).
static int64_t ReceiveHandshake(TlsSession* s, int64_t deadline, char* error, int64_t capacity)
{
	const int64_t got = ReceiveMore(s, Remaining(deadline), error, capacity);
	if (got > 0) return 0;
	if (got == kNetTimedOut) c4_copy_text("the TLS handshake timed out", error, capacity);
	if (got != 0) return got;
	char text[kTlsReasonBytes];
	if (s->server) snprintf(text, sizeof(text), "the client closed the connection during the TLS handshake");
	else snprintf(text, sizeof(text), "%s closed the connection during the TLS handshake (is it serving TLS on this port?)", s->host);
	return c4_tls_posix_refused(text, error, capacity);
}

// The handshake, step by step: each step's output sent (an alert, too, on a failure), more input received while
// OpenSSL wants it.
static int64_t Handshake(TlsSession* s, int64_t deadline, char* error, int64_t capacity)
{
	for (;;)
	{
		const TlsStep step = Call(s, kCallHandshake, NULL, 0);
		const bool failed = step.result != 1 && step.status != kSslErrorWantRead;
		char ignored[kTlsReasonBytes];
		const int64_t sent = SendWritten(s, Remaining(deadline), failed ? ignored : error, failed ? (int64_t)sizeof(ignored) : capacity);
		if (failed) return HandshakeFailed(s, step.failure, error, capacity);
		if (sent < 0) return sent;
		if (step.result == 1) return c4_tls_posix_done(0, error, capacity);
		const int64_t got = ReceiveHandshake(s, deadline, error, capacity);
		if (got != 0) return got;
	}
}

// --- client and server -----------------------------------------------------------------------------------------------

// A client session's host name (sent as SNI unless it is an address, which SNI cannot carry), the name its server's
// certificate must carry, and its extra roots; 0, or -1 with why.
static int64_t PrepareClient(TlsSession* s, const c4_tls_client_options* options, char* error, int64_t capacity)
{
	const TlsApi* api = s->api;
	if (strlen(options->hostName) >= sizeof(s->host)) return c4_tls_posix_refused("the host name is longer than 255 bytes", error, capacity);
	c4_copy_text(options->hostName, s->host, sizeof(s->host));
	if (options->extraRootsSize > 0 && !c4_tls_posix_load_roots(api, options->extraRoots, options->extraRootsSize, &s->extraRoots, error, capacity))
		return kNetError;
	const bool named = c4_tls_posix_is_ip_address(s->host) || api->SSL_ctrl(s->ssl, kSslCtrlSetTlsextHostname, kTlsextNametypeHostName, s->host) == 1;
	if (!named || !c4_tls_posix_expect_host(api, api->SSL_get0_param(s->ssl), s->host))
		return c4_tls_posix_failed(api, "OpenSSL refused the host name", c4_tls_posix_take_error(api), error, capacity);
	return 0;
}

static int64_t CheckServer(TlsSession* s, char* error, int64_t capacity)
{
	const TlsServerCheck check = { s->host, s->extraRoots };
	return c4_tls_posix_check_server(s->api, s->ssl, &check, error, capacity);
}

int64_t c4_tls_connect(int64_t socket, const c4_tls_client_options* options, int32_t timeoutMs, char* error, int64_t errorCapacity)
{
	if (socket < 0 || !options || !options->hostName || !*options->hostName)
		return c4_tls_posix_refused("a connected socket and a host name are needed", error, errorCapacity);
	if (options->extraRootsSize < 0 || (options->extraRootsSize > 0 && !options->extraRoots))
		return c4_tls_posix_refused("the extra roots are malformed", error, errorCapacity);
	const TlsApi* api = c4_tls_posix_api(error, errorCapacity);
	if (!api) return kNetError;
	SSL_CTX* context = c4_tls_posix_client_context(error, errorCapacity);
	if (!context) return kNetError;
	TlsSession* s = NewSession(api, context, socket, false);
	int64_t result = Opened(s, error, errorCapacity);
	if (!s) return result;
	const int64_t deadline = DeadlineAfter(timeoutMs);
	if (result == 0) result = PrepareClient(s, options, error, errorCapacity);
	if (result == 0) result = Handshake(s, deadline, error, errorCapacity);
	if (result == 0) result = CheckServer(s, error, errorCapacity);
	if (result != 0) return Abandon(s, result);
	LearnConnection(s);
	return c4_tls_posix_done((int64_t)(intptr_t)s, error, errorCapacity);
}

int64_t c4_tls_accept(int64_t socket, int64_t identity, int32_t timeoutMs, char* error, int64_t errorCapacity)
{
	const TlsIdentity* id = identity == -1 ? NULL : (const TlsIdentity*)(intptr_t)identity;
	if (socket < 0 || !id) return c4_tls_posix_refused("an accepted socket and an identity are needed", error, errorCapacity);
	TlsSession* s = NewSession(id->api, id->context, socket, true);
	int64_t result = Opened(s, error, errorCapacity);
	if (!s) return result;
	if (result == 0) result = Handshake(s, DeadlineAfter(timeoutMs), error, errorCapacity);
	if (result != 0) return Abandon(s, result);
	LearnConnection(s);
	return c4_tls_posix_done((int64_t)(intptr_t)s, error, errorCapacity);
}

// --- sending ---------------------------------------------------------------------------------------------------------

// Up to one record of `data` encrypted and sent, with the send lock held: the bytes it took, or the failure (a record
// cut off breaks the session).
static int64_t SendRecord(TlsSession* s, const void* data, int64_t size, int32_t timeoutMs, char* error, int64_t capacity)
{
	const int count = size < kMostRecordBytes ? (int)size : kMostRecordBytes;
	const TlsStep step = Call(s, kCallWrite, (void*)data, count);
	if (step.result <= 0) return c4_tls_posix_failed(s->api, "a TLS record could not be encrypted", step.failure, error, capacity);
	const int64_t sent = SendWritten(s, timeoutMs, error, capacity);
	if (sent == 0) return c4_tls_posix_done(step.result, error, capacity);
	snprintf(s->sendFailure, sizeof(s->sendFailure), "a TLS record was cut off (%s): the session cannot send again", sent == kNetTimedOut ? "timed out" : error);
	return sent;
}

// The send with its lock held: refused once the session cannot send, timed out with nothing encrypted while the
// socket takes nothing.
static int64_t SendHeld(TlsSession* s, const void* data, int64_t size, int32_t timeoutMs, char* error, int64_t capacity)
{
	if (s->sendFailure[0]) return c4_tls_posix_refused(s->sendFailure, error, capacity);
	if (s->closeSent) return c4_tls_posix_refused("this side has closed the TLS session", error, capacity);
	if (size == 0) return c4_tls_posix_done(0, error, capacity);
	const int writable = WaitWritable(s->socket, timeoutMs);
	if (writable < 0) return c4_tls_posix_refused("the connection failed", error, capacity);
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
	if (!s || size < 0 || (size > 0 && !data)) return c4_tls_posix_refused("nothing to send", error, errorCapacity);
	pthread_mutex_lock(&s->sendLock);
	const int64_t result = SendHeld(s, data, size, timeoutMs, error, errorCapacity);
	pthread_mutex_unlock(&s->sendLock);
	return result;
}

// --- receiving -------------------------------------------------------------------------------------------------------

// What a receive made OpenSSL write (an answer to a message after the handshake, or an alert), sent before any other
// send: at least kAnswerMs for each piece, past a receive's shorter wait, since the peer may wait for it. When it
// cannot go, the session cannot go on.
static bool SendAnswer(TlsSession* s, int64_t deadline, char* error, int64_t capacity)
{
	const int64_t least = DeadlineAfter(kAnswerMs);
	if (deadline != kForever && deadline < least) deadline = least;
	pthread_mutex_lock(&s->sendLock);
	const int64_t sent = SendWritten(s, Remaining(deadline), error, capacity);
	if (sent != 0)
	{
		snprintf(s->receiveFailure, sizeof(s->receiveFailure), "an answer to the peer's TLS message could not be sent (%s): the session cannot go on", sent == kNetTimedOut ? "timed out" : error);
		c4_copy_text(s->receiveFailure, s->sendFailure, sizeof(s->sendFailure));
	}
	pthread_mutex_unlock(&s->sendLock);
	return sent == 0;
}

// Why a read failed, kept for every later receive. A TLS 1.3 server that wants a client certificate says so after the
// client's side of the handshake is done, so on the first receive.
static void KeepReadFailure(TlsSession* s, unsigned long failure)
{
	enum { kDecryptionFailed = 147, kDecryptionFailedOrBadRecordMac = 281, kAlertCertificateRequired = 116 };
	const TlsApi* api = s->api;
	const int reason = c4_tls_posix_error_library(api, failure) == kTlsLibrarySsl ? c4_tls_posix_error_reason(api, failure) : 0;
	const char* what = reason == kDecryptionFailed || reason == kDecryptionFailedOrBadRecordMac ? "a TLS record could not be decrypted (altered or corrupt)"
		: reason == kSslAlertReasonOffset + kAlertCertificateRequired ? "the server asks for a client certificate, which this client does not have"
		: IsAlert(api, failure) ? "the peer ended the TLS session with an alert" : "a TLS record could not be read";
	c4_tls_posix_failed(api, what, failure, s->receiveFailure, sizeof(s->receiveFailure));
}

// The peer closed the connection: between records that is the end, inside one a failure.
static void KeepPeerEnd(TlsSession* s)
{
	if (RecordUnfinished(s)) c4_copy_text("the connection ended inside a TLS record", s->receiveFailure, sizeof(s->receiveFailure));
	else s->receiveEnded = true;
}

// One step towards plaintext: reading what OpenSSL has, or receiving more for it. true to go on, false with `*result`.
static bool ReceiveStep(TlsSession* s, void* buffer, int64_t capacity, int64_t deadline, int64_t* result, char* error, int64_t errorCapacity)
{
	const TlsStep step = Call(s, kCallRead, buffer, capacity > INT_MAX ? INT_MAX : (int)capacity);
	if (step.answered && !SendAnswer(s, deadline, error, errorCapacity) && step.result <= 0) return true;
	if (step.result > 0)
	{
		*result = c4_tls_posix_done(step.result, error, errorCapacity);
		return false;
	}
	if (step.status == kSslErrorZeroReturn) s->receiveEnded = true;
	else if (step.status != kSslErrorWantRead) KeepReadFailure(s, step.failure);
	else
	{
		const int64_t got = ReceiveMore(s, Remaining(deadline), error, errorCapacity);
		if (got == 0) KeepPeerEnd(s);
		else if (got < 0)
		{
			*result = got;
			return false;
		}
	}
	return true;
}

// The receive with its lock held.
static int64_t ReceiveHeld(TlsSession* s, void* buffer, int64_t capacity, int32_t timeoutMs, char* error, int64_t errorCapacity)
{
	const int64_t deadline = DeadlineAfter(timeoutMs);
	int64_t result = 0;
	for (;;)
	{
		if (s->receiveFailure[0]) return c4_tls_posix_refused(s->receiveFailure, error, errorCapacity);
		if (s->receiveEnded) return c4_tls_posix_done(0, error, errorCapacity);
		if (!ReceiveStep(s, buffer, capacity, deadline, &result, error, errorCapacity)) return result;
	}
}

int64_t c4_tls_receive(int64_t session, void* buffer, int64_t capacity, int32_t timeoutMs, char* error, int64_t errorCapacity)
{
	TlsSession* s = SessionOf(session);
	if (!s || capacity <= 0 || !buffer) return c4_tls_posix_refused("no room to receive into", error, errorCapacity);
	pthread_mutex_lock(&s->receiveLock);
	const int64_t result = ReceiveHeld(s, buffer, capacity, timeoutMs, error, errorCapacity);
	pthread_mutex_unlock(&s->receiveLock);
	return result;
}

// --- ending ----------------------------------------------------------------------------------------------------------

// The close_notify alert, with the send lock held: 0 when it went out.
static int32_t SendCloseNotify(TlsSession* s)
{
	const TlsStep step = Call(s, kCallShutdown, NULL, 0);
	s->closeSent = true;
	if (step.result < 0) return kNetError;
	char ignored[kTlsReasonBytes];
	return step.answered && SendWritten(s, 0, ignored, sizeof(ignored)) == 0 ? 0 : kNetError;
}

int32_t c4_tls_close_notify(int64_t session)
{
	TlsSession* s = SessionOf(session);
	if (!s || pthread_mutex_trylock(&s->sendLock) != 0) return kNetError;
	const bool possible = !s->closeSent && !s->sendFailure[0] && WaitWritable(s->socket, 0) == 1;
	const int32_t result = possible ? SendCloseNotify(s) : kNetError;
	pthread_mutex_unlock(&s->sendLock);
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
	pthread_mutex_lock(&s->receiveLock);
	pthread_mutex_lock(&s->sendLock);
	pthread_mutex_unlock(&s->sendLock);
	pthread_mutex_unlock(&s->receiveLock);
	FreeSession(s);
}
