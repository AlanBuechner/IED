// Fetch: an HTTP/1.1 client for one request at a time (D-66) - a connection per request, closed after the response.
// http, and https through TLS (D-84): the server's certificate is checked against the system's roots, or extra ones a
// caller trusts (Net::TlsClientOptions).
//     Http::FetchResult got = Http::Get(Core::StringView("https://example.com/status"), 5000);
//     if (got.ok && got.response.status == 200) printf("%s\n", got.response.BodyText().Data());

include "message.ch"
include "parse.ch"
include "url.ch"
include "Net/tcp.ch"

namespace Http
{
	// The largest response head and body a fetch reads.
	const int64 kMaxFetchHeadBytes = 64 * 1024;
	const int64 kMaxFetchBodyBytes = 256 * 1024 * 1024;

	// What a fetch came to: the response, or why there is none.
	struct FetchResult
	{
		public bool ok;
		public Response response;
		public Core::String error;
	}

	// A connection to `url`'s server: its host resolved, the first of its addresses that answers, and over TLS when the
	// URL is secure, the certificate checked against `tls` (whose host name, when empty, is the URL's). A closed stream
	// with `why` when none can be made.
	Net::TcpStream ConnectTo(const Url& url, const Net::TlsClientOptions& tls, int32 timeoutMs, Core::String& why);
	// Reads a response head off `stream` into `response`: where its body starts in `received`, or -1 with `why`.
	// Interim 1xx responses are skipped, but a 101 (the switch a WebSocket asked for) is returned.
	int64 ReadSwitchHead(Net::TcpStream& stream, Core::Array<uint8>& received, int32 timeoutMs, Response& response, Core::String& why);

	// Sends `request` - its method, target, headers and body (Host defaults to the address) - to `at` over plain TCP and
	// reads the response, skipping interim 1xx ones. Every wait is bounded by `timeoutMs`.
	FetchResult Fetch(const Net::Address& at, const Request& request, int32 timeoutMs);
	// Sends `request` to `url` (http:// or https://; ConnectTo), its target and Host header taken from the URL.
	FetchResult FetchUrl(Core::StringView url, Request request, const Net::TlsClientOptions& tls, int32 timeoutMs);
	// FetchUrl with the system's roots only.
	FetchResult Get(Core::StringView url, int32 timeoutMs);
	FetchResult Post(Core::StringView url, Core::StringView contentType, Core::StringView body, int32 timeoutMs);
}
