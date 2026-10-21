// Fetch: an HTTP/1.1 client for one request at a time (D-66) - a connection per request, closed after the response.
// Plain http only: no TLS.
//     Http::FetchResult got = Http::Get(Core::StringView("http://127.0.0.1:8080/status"), 2000);
//     if (got.ok && got.response.status == 200) printf("%s\n", got.response.BodyText().Data());

include "message.ch"
include "parse.ch"
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

	// The parts of "http://host[:port][/path][?query]" or a ws:// URL; `valid` is false for anything else (another
	// scheme, no host, a bad port). An IPv6 host is written in brackets: "http://[::1]:8080/".
	struct Url
	{
		public bool valid;
		public bool webSocket;        // ws:// rather than http://
		public Core::String host;     // without brackets
		public int32 port;            // 80 when the URL gives none
		public Core::String target;   // "/path?query"; "/" when the URL has no path
	}

	Url ParseUrl(Core::StringView text);
	// The Host header for `url`: its host, in brackets when IPv6, and its port unless it is 80.
	Core::String HostHeader(const Url& url);
	// Reads a response head off `stream` into `response`: where its body starts in `received`, or -1 with `why`.
	// Interim 1xx responses are skipped, but a 101 (the switch a WebSocket asked for) is returned.
	int64 ReadSwitchHead(Net::TcpStream& stream, Core::Array<uint8>& received, int32 timeoutMs, Response& response, Core::String& why);

	// Sends `request` - its method, target, headers and body (Host defaults to the address) - to `at` and reads the
	// response, skipping interim 1xx ones. Every wait is bounded by `timeoutMs`.
	FetchResult Fetch(const Net::Address& at, const Request& request, int32 timeoutMs);
	FetchResult Get(Core::StringView url, int32 timeoutMs);
	FetchResult Post(Core::StringView url, Core::StringView contentType, Core::StringView body, int32 timeoutMs);
}
