// Http: HTTP/1.1 messages (D-66). Headers keep the order they came in and are found by name without regard to case;
// a body is bytes, with BodyText() to read it as text.
//     Http::Response response;
//     response.SetText("application/json", Core::StringView("{\"ok\":true}"));
//     response.headers.Set("Cache-Control", "no-store");

include "Container/array.ch"
include "Net/tcp.ch"
include "Text/string.ch"
include "Text/stringview.ch"
include "Threading/atomic.ch"

namespace Http
{
	struct Header
	{
		public Core::String name;
		public Core::String value;
	}

	// ASCII letters compared without case: header names, methods' tokens and codings are ASCII.
	bool EqualsIgnoringCase(Core::StringView a, Core::StringView b);
	// Whether a comma-separated header value (Connection, Upgrade) lists `token`, without regard to case. False for null.
	bool ListsToken(const Core::String* list, const char* token);

	// A message's headers in their order; names compare without regard to case.
	class Headers
	{
		// The value of the first header called `name`, or null.
		public const Core::String* Find(Core::StringView name) const;
		public bool Has(Core::StringView name) const { return Find(name) != nullptr; }
		// One header called `name` with `value`, in place of any there were.
		public void Set(Core::StringView name, Core::StringView value);
		// One more header, keeping those already there (as Set-Cookie needs).
		public void Add(Core::StringView name, Core::StringView value);
		public void Remove(Core::StringView name);
		public void Clear() { list.Clear(); }

		public int64 Count() const { return list.Count(); }
		public const Header& At(int64 index) const { return list[index]; }

		private Core::Array<Header> list;
	}

	class Request
	{
		public Core::String method;   // "GET", "POST", ...: case matters
		public Core::String target;   // as sent: "/files/a.txt?x=1"
		public Core::String path;     // the target before any '?', not percent-decoded: "/files/a.txt"
		public Core::String query;    // after the '?', without it: "x=1"
		public Net::Address peer;     // where the connection came from (Http::Server); not valid on a parsed head alone
		public int32 minorVersion;    // HTTP/1.<minorVersion>: 1, or 0 for an HTTP/1.0 client
		public Headers headers;
		public Core::Array<uint8> body;

		public Core::StringView BodyText() const { return Core::StringView((const char*)body.Data(), body.Count()); }
		public void SetBody(Core::StringView text);
	}

	// A protocol a connection switches to (WebSocket, AcceptWebSocket): after a 101 response the server calls it on the
	// connection's thread with the connection's stream, the bytes received after the request, and the server's stop flag,
	// and closes the connection when it returns.
	alias Takeover = func<void(Net::TcpStream&, Core::Array<uint8>&, const Threading::Atomic<bool>*)>;

	// Reads the body's bytes from `offset` into `into`, at most `capacity` of them; how many it read, 0 or less when it
	// cannot go on (the server then closes the connection, the response cut short).
	alias BodyReader = func<int64(int64 offset, uint8* into, int64 capacity)>;

	// A body the server sends a piece at a time as `read` gives them, in place of Response::body: a file far larger than
	// memory (a video), read as it is sent. With `length` -1 its length is not known (a program's output): it goes
	// chunked until `read` gives 0. `finished`, when set, is called once sending ends, with whether the whole body went
	// (false when the client left first): the place to stop what produces it.
	struct StreamedBody
	{
		public int64 length;
		public BodyReader read;
		public func<void(bool)> finished;
	}

	class Response
	{
		[operator("create")] public void Init() { status = 200; }

		public int32 status;
		public Headers headers;
		public Core::Array<uint8> body;
		public Takeover takeover;   // with status 101
		public StreamedBody streamed;   // with `read` set, the body (and `body` is not sent)

		public Core::StringView BodyText() const { return Core::StringView((const char*)body.Data(), body.Count()); }
		// The body, and a Content-Type saying what it is.
		public void SetText(Core::StringView contentType, Core::StringView text);
		public bool IsStreamed() const { return streamed.read ? true : false; }
		// The length of the body sent: the streamed one's (-1 when not known), or `body`'s.
		public int64 BodyLength() const { return IsStreamed() ? streamed.length : body.Count(); }
	}

	// "OK", "Not Found", ...; "Unknown" for a code with no standard reason.
	const char* ReasonPhrase(int32 status);
}
