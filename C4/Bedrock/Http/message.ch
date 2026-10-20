// Http: HTTP/1.1 messages (D-66). Headers keep the order they came in and are found by name without regard to case;
// a body is bytes, with BodyText() to read it as text.
//     Http::Response response;
//     response.SetText("application/json", Core::StringView("{\"ok\":true}"));
//     response.headers.Set("Cache-Control", "no-store");

include "../Container/array.ch"
include "../Text/string.ch"
include "../Text/stringview.ch"

namespace Http
{
	struct Header
	{
		public Core::String name;
		public Core::String value;
	}

	// ASCII letters compared without case: header names, methods' tokens and codings are ASCII.
	bool EqualsIgnoringCase(Core::StringView a, Core::StringView b);

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
		public int32 minorVersion;    // HTTP/1.<minorVersion>: 1, or 0 for an HTTP/1.0 client
		public Headers headers;
		public Core::Array<uint8> body;

		public Core::StringView BodyText() const { return Core::StringView((const char*)body.Data(), body.Count()); }
		public void SetBody(Core::StringView text);
	}

	class Response
	{
		[operator("create")] public void Init() { status = 200; }

		public int32 status;
		public Headers headers;
		public Core::Array<uint8> body;

		public Core::StringView BodyText() const { return Core::StringView((const char*)body.Data(), body.Count()); }
		// The body, and a Content-Type saying what it is.
		public void SetText(Core::StringView contentType, Core::StringView text);
	}

	// "OK", "Not Found", ...; "Unknown" for a code with no standard reason.
	const char* ReasonPhrase(int32 status);
}
