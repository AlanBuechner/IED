// AuthGate: the HTTP side of authentication (docs/11) - the session cookie on each request, the launch-token exchange,
// /api/auth/* and what a request without a session gets. It runs on a connection's thread and takes the server lock
// as it needs it.

include "json.ch"
include "auth/devices.ch"

namespace Ied
{
	class AuthGate
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server) { this.server = server; }

		// The device the request's session cookie names, or null; sends the cookie again on `response` when it is due.
		public Device* DeviceOf(const Http::Request& request, Http::Response& response);
		// A GET with `?t=<token>`: spends a live token and redirects to the path without it (303, with the cookie).
		// False when there is no live token in the query: the request goes on as if it had none.
		public bool Exchange(const Http::Request& request, Http::Response& response);
		// /api/auth/state, /api/auth/login, /api/auth/logout.
		public void Api(Core::StringView path, const Http::Request& request, Http::Response& response);
		// The files anyone may fetch, so the login page can load.
		public bool IsPublic(Core::StringView path) const;

	private:
		private void State(const Http::Request& request, Http::Response& response);
		private void Login(const Http::Request& request, Http::Response& response);
		private void Logout(const Http::Request& request, Http::Response& response);
		private Core::String CookieName() const;
		private void SetCookie(Http::Response& response, Core::StringView value, int64 maxAgeS) const;

		private Server* server;
	}

	// The value of cookie `name` in a Cookie header ("a=1; b=2"), or empty.
	Core::StringView CookieValue(Core::StringView header, Core::StringView name);
	// The value of `name` in a query string ("t=abc&x=1"), not decoded; empty when absent.
	Core::StringView QueryValue(Core::StringView query, Core::StringView name);
	// "Edge on Windows", "Safari on iOS", ... from a User-Agent; "Unknown browser" when it names none.
	Core::String DeviceNameFrom(Core::StringView userAgent);
	// A JSON body: status, Content-Type and the text.
	void AnswerJson(Http::Response& response, int32 status, Core::StringView json);
}
